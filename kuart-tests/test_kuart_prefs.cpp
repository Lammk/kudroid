// Probe: Java file I/O and SharedPreferences, run through the real framework.dex.
//
// Two failures this pins, both of which were silent — the reason they survived is that
// neither reported anything at all:
//
// 1. FileOutputStream.write was an EMPTY METHOD. An app wrote its data, got no exception,
//    and no file appeared. Reading it back produced the default value, which looks like the
//    data was never stored rather than like the write was dropped. FileInputStream.read
//    returned -1 unconditionally, indistinguishable from an empty file. The natives existed
//    in LibCore.cpp the whole time; no Java class declared them, so nothing called them.
//
// 2. Java file paths did not go through the VFS remapper, while a guest's own native code
//    did. getFilesDir() returns /data/data/<pkg>/files, so Java aimed at the REAL path —
//    which iOS does not allow anyone to write — and File.mkdirs() swallows the failure.
//
// And what depends on both: SharedPreferences persistence. In-memory is not merely
// incomplete here. An app that generates an identifier once and stores it (Minecraft's
// getLegacyDeviceID) gets a NEW one on every launch, so the install looks like a different
// device each time. That is the bug the device log showed, one step removed.
//
// HOME is redirected to a temporary directory, so the remapper builds its tree there and
// the test never touches a real android_root.
#include "kudroid/framework_dex_bytes.h"
#include "kudroid/VFSPathRemapper.h"
#include "kudroid/kuart/DexClassLinker.h"
#include "kudroid/kuart/DexJniEnv.h"
#include "kudroid/kuart/DexObject.h"
#include "kudroid/kuart/DexString.h"
#include "kudroid/kuart/Interpreter.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <unistd.h>

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    std::printf("%s %s\n", ok ? "  OK  " : "  FAIL", what.c_str());
    if (!ok) ++g_failures;
}

using kudroid::kuart::DexClass;
using kudroid::kuart::DexClassLinker;
using kudroid::kuart::DexField;
using kudroid::kuart::DexJniEnv;
using kudroid::kuart::DexMethod;
using kudroid::kuart::DexObject;
using kudroid::kuart::DexString;
using kudroid::kuart::DexValue;
using kudroid::kuart::Interpreter;

DexClassLinker* g_linker = nullptr;
Interpreter* g_interp = nullptr;

DexValue Str(const char* s) {
    return DexValue::Ref(reinterpret_cast<DexObject*>(g_linker->NewString(s)));
}

const char* Utf8Of(const DexValue& v) {
    auto* s = reinterpret_cast<DexString*>(v.l);
    return (s != nullptr && s->utf8 != nullptr) ? s->utf8 : "(null)";
}

// A byte[] holding `text`, for the write path.
DexValue ByteArray(const std::string& text) {
    DexClass* byteArray = g_linker->FindClass("[B");
    if (byteArray == nullptr) return DexValue();
    auto* arr = g_linker->AllocArray(byteArray, static_cast<int32_t>(text.size()));
    if (arr == nullptr) return DexValue();
    auto* bytes = reinterpret_cast<uint8_t*>(arr + 1);
    std::memcpy(bytes, text.data(), text.size());
    return DexValue::Ref(reinterpret_cast<DexObject*>(arr));
}

bool CallStatic(const char* descriptor, const char* name, const char* sig,
                std::vector<DexValue> args, DexValue* out, const char* what) {
    DexClass* klass = g_linker->FindClass(descriptor);
    if (klass == nullptr || klass->is_stub) {
        std::printf("  FAIL %s: class %s not in framework.dex\n", what, descriptor);
        ++g_failures; ++g_checks;
        return false;
    }
    g_interp->ClearPendingException();
    if (!g_interp->EnsureInitialized(klass)) {
        std::printf("  FAIL %s: <clinit> of %s failed: %s\n", what, descriptor,
                    g_interp->last_error().c_str());
        ++g_failures; ++g_checks;
        g_interp->ClearPendingException();
        return false;
    }
    DexMethod* m = klass->FindDirectMethod(name, sig);
    if (m == nullptr) m = klass->FindVirtualMethod(name, sig);
    if (m == nullptr) {
        std::printf("  FAIL %s: no method %s%s on %s\n", what, name, sig, descriptor);
        ++g_failures; ++g_checks;
        return false;
    }
    const DexValue r = g_interp->Execute(m, args.data(), args.size());
    if (g_interp->HasPendingException()) {
        std::printf("  FAIL %s threw: %s\n", what, g_interp->last_error().c_str());
        ++g_failures; ++g_checks;
        g_interp->ClearPendingException();
        return false;
    }
    if (out != nullptr) *out = r;
    return true;
}

bool CallVirtual(DexObject* receiver, const char* name, const char* sig,
                 std::vector<DexValue> args, DexValue* out, const char* what) {
    if (receiver == nullptr || receiver->clazz == nullptr) {
        std::printf("  FAIL %s: null receiver\n", what);
        ++g_failures; ++g_checks;
        return false;
    }
    DexMethod* m = receiver->clazz->FindVirtualMethod(name, sig);
    if (m == nullptr) m = receiver->clazz->FindDirectMethod(name, sig);
    if (m == nullptr) {
        std::printf("  FAIL %s: no method %s%s on %s\n", what, name, sig,
                    receiver->clazz->PrettyName().c_str());
        ++g_failures; ++g_checks;
        return false;
    }
    std::vector<DexValue> full;
    full.push_back(DexValue::Ref(receiver));
    for (const DexValue& v : args) full.push_back(v);

    g_interp->ClearPendingException();
    const DexValue r = g_interp->Execute(m, full.data(), full.size());
    if (g_interp->HasPendingException()) {
        std::printf("  FAIL %s threw: %s\n", what, g_interp->last_error().c_str());
        ++g_failures; ++g_checks;
        g_interp->ClearPendingException();
        return false;
    }
    if (out != nullptr) *out = r;
    return true;
}

DexObject* NewObject(const char* descriptor, const char* ctorSig,
                     std::vector<DexValue> args, const char* what) {
    DexClass* klass = g_linker->FindClass(descriptor);
    if (klass == nullptr || klass->is_stub) {
        std::printf("  FAIL %s: class %s not in framework.dex\n", what, descriptor);
        ++g_failures; ++g_checks;
        return nullptr;
    }
    g_interp->ClearPendingException();
    if (!g_interp->EnsureInitialized(klass)) {
        std::printf("  FAIL %s: <clinit> failed: %s\n", what,
                    g_interp->last_error().c_str());
        ++g_failures; ++g_checks;
        g_interp->ClearPendingException();
        return nullptr;
    }
    DexObject* obj = g_linker->AllocObject(klass);
    if (obj == nullptr) {
        std::printf("  FAIL %s: AllocObject failed\n", what);
        ++g_failures; ++g_checks;
        return nullptr;
    }
    DexMethod* ctor = klass->FindDirectMethod("<init>", ctorSig);
    if (ctor == nullptr) {
        std::printf("  FAIL %s: no <init>%s\n", what, ctorSig);
        ++g_failures; ++g_checks;
        return nullptr;
    }
    std::vector<DexValue> full;
    full.push_back(DexValue::Ref(obj));
    for (const DexValue& v : args) full.push_back(v);

    g_interp->Execute(ctor, full.data(), full.size());
    if (g_interp->HasPendingException()) {
        std::printf("  FAIL %s ctor threw: %s\n", what, g_interp->last_error().c_str());
        ++g_failures; ++g_checks;
        g_interp->ClearPendingException();
        return nullptr;
    }
    return obj;
}

// ── file I/O ────────────────────────────────────────────────────────────────

void TestFileWriteThenRead(const std::string& androidRoot) {
    std::printf("-- FileOutputStream actually writes --\n");

    // A guest path: this is what getFilesDir() returns, and it only works if the natives
    // remap it. Without the remap it names the real /data/data, which does not exist here
    // and is unwritable on device.
    const char* guestPath = "/data/data/com.kudroid.test/files/probe.txt";
    const std::string hostPath = androidRoot + "/data/data/com.kudroid.test/files/probe.txt";

    // The parent has to be creatable through Java too, since that is how an app does it.
    DexObject* dir = NewObject("Ljava/io/File;", "(Ljava/lang/String;)V",
                               {Str("/data/data/com.kudroid.test/files")}, "new File(dir)");
    if (dir != nullptr) {
        DexValue made;
        CallVirtual(dir, "mkdirs", "()Z", {}, &made, "File.mkdirs");
        DexValue exists;
        if (CallVirtual(dir, "exists", "()Z", {}, &exists, "File.exists after mkdirs")) {
            Check(exists.i == 1, "mkdirs created the directory (remapped, so writable)");
        }
        Check(std::filesystem::is_directory(
                  androidRoot + "/data/data/com.kudroid.test/files"),
              "the directory appeared under android_root, not at the literal path");
    }

    const std::string payload = "device-id=6ba7b810-9dad-11d1-80b4-00c04fd430c8\n";

    DexObject* os = NewObject("Ljava/io/FileOutputStream;", "(Ljava/lang/String;)V",
                              {Str(guestPath)}, "new FileOutputStream");
    if (os != nullptr) {
        DexValue bytes = ByteArray(payload);
        CallVirtual(os, "write", "([B)V", {bytes}, nullptr, "FileOutputStream.write");
        CallVirtual(os, "close", "()V", {}, nullptr, "FileOutputStream.close");

        // The file must exist ON THE HOST with the right bytes. Checking only through Java
        // would pass even if both sides were consistently broken.
        Check(std::filesystem::exists(hostPath), "the file exists on the host filesystem");
        if (std::filesystem::exists(hostPath)) {
            std::ifstream in(hostPath, std::ios::binary);
            const std::string got((std::istreambuf_iterator<char>(in)), {});
            Check(got == payload, "the bytes on disk are exactly what was written");
            Check(got.size() == payload.size(),
                  "no truncation (" + std::to_string(got.size()) + " of " +
                      std::to_string(payload.size()) + " bytes)");
        }
    }

    std::printf("-- FileInputStream actually reads --\n");

    DexObject* is = NewObject("Ljava/io/FileInputStream;", "(Ljava/lang/String;)V",
                              {Str(guestPath)}, "new FileInputStream");
    if (is != nullptr) {
        DexValue available;
        if (CallVirtual(is, "available", "()I", {}, &available, "FileInputStream.available")) {
            Check(available.i == static_cast<int32_t>(payload.size()),
                  "available() reports the file size, not 0");
        }
        // Read the whole file back through Java and compare byte for byte.
        DexClass* byteArray = g_linker->FindClass("[B");
        auto* buf = byteArray != nullptr ? g_linker->AllocArray(byteArray, 256) : nullptr;
        DexValue n;
        if (CallVirtual(is, "read", "([BII)I",
                        {DexValue::Ref(reinterpret_cast<DexObject*>(buf)), DexValue::Int(0),
                         DexValue::Int(256)},
                        &n, "FileInputStream.read")) {
            Check(n.i == static_cast<int32_t>(payload.size()),
                  "read() returned the byte count, not -1");
            if (n.i > 0) {
                const auto* got = reinterpret_cast<const char*>(buf + 1);
                Check(std::string(got, static_cast<size_t>(n.i)) == payload,
                      "the bytes read back match the bytes written");
            }
        }
        CallVirtual(is, "close", "()V", {}, nullptr, "FileInputStream.close");
    }

    // A byte of 0xFF must not read as end-of-file. read() returns 0..255, and without the
    // mask a signed byte of -1 becomes the EOF sentinel — so a binary file appears to end
    // at its first 0xFF.
    std::printf("-- a 0xFF byte is not end-of-file --\n");
    {
        const std::string binaryPath = androidRoot + "/data/data/com.kudroid.test/files/bin";
        {
            std::ofstream out(binaryPath, std::ios::binary);
            const unsigned char raw[] = {0x41, 0xFF, 0x42};
            out.write(reinterpret_cast<const char*>(raw), sizeof(raw));
        }
        DexObject* bin = NewObject("Ljava/io/FileInputStream;", "(Ljava/lang/String;)V",
                                   {Str("/data/data/com.kudroid.test/files/bin")},
                                   "new FileInputStream(binary)");
        if (bin != nullptr) {
            DexValue a, b, c;
            CallVirtual(bin, "read", "()I", {}, &a, "read() 1");
            CallVirtual(bin, "read", "()I", {}, &b, "read() 2");
            CallVirtual(bin, "read", "()I", {}, &c, "read() 3");
            Check(a.i == 0x41, "the first byte reads as 0x41");
            Check(b.i == 0xFF, "a 0xFF byte reads as 255, not -1");
            Check(c.i == 0x42, "reading continues past the 0xFF byte");
            CallVirtual(bin, "close", "()V", {}, nullptr, "close(binary)");
        }
    }

    // Opening a missing file must throw, not hand back a stream that reads zero bytes —
    // which a caller cannot distinguish from a legitimately empty file.
    std::printf("-- a missing file throws --\n");
    {
        DexClass* klass = g_linker->FindClass("Ljava/io/FileInputStream;");
        DexObject* obj = klass != nullptr ? g_linker->AllocObject(klass) : nullptr;
        DexMethod* ctor =
            klass != nullptr ? klass->FindDirectMethod("<init>", "(Ljava/lang/String;)V")
                             : nullptr;
        if (obj != nullptr && ctor != nullptr) {
            g_interp->ClearPendingException();
            std::vector<DexValue> args{DexValue::Ref(obj),
                                       Str("/data/data/com.kudroid.test/files/absent")};
            g_interp->Execute(ctor, args.data(), args.size());
            Check(g_interp->HasPendingException(),
                  "opening a missing file raises an exception");
            g_interp->ClearPendingException();
        }
    }
}

// ── SharedPreferences ───────────────────────────────────────────────────────

DexObject* NewPrefs(const std::string& guestDir, const char* name, const char* what) {
    DexObject* dir = NewObject("Ljava/io/File;", "(Ljava/lang/String;)V",
                              {Str(guestDir.c_str())}, "new File(prefs dir)");
    if (dir == nullptr) return nullptr;
    return NewObject("Landroid/content/SharedPreferencesImpl;",
                     "(Ljava/lang/String;Ljava/io/File;)V",
                     {Str(name), DexValue::Ref(dir)}, what);
}

void TestPreferencesPersist(const std::string& androidRoot) {
    std::printf("-- preferences survive a new instance --\n");

    const std::string guestDir = "/data/data/com.kudroid.test/shared_prefs";
    std::filesystem::create_directories(androidRoot + guestDir);

    // Write through one instance...
    DexObject* prefs = NewPrefs(guestDir, "settings", "new SharedPreferencesImpl");
    if (prefs == nullptr) return;

    DexValue editor;
    if (!CallVirtual(prefs, "edit", "()Landroid/content/SharedPreferences$Editor;", {},
                     &editor, "edit()")) {
        return;
    }
    DexObject* ed = editor.l;
    CallVirtual(ed, "putString",
                "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/SharedPreferences$Editor;",
                {Str("device_id"), Str("6ba7b810-9dad-11d1-80b4-00c04fd430c8")}, nullptr,
                "putString");
    CallVirtual(ed, "putInt", "(Ljava/lang/String;I)Landroid/content/SharedPreferences$Editor;",
                {Str("launches"), DexValue::Int(7)}, nullptr, "putInt");
    CallVirtual(ed, "putLong", "(Ljava/lang/String;J)Landroid/content/SharedPreferences$Editor;",
                {Str("installed_at"), DexValue::Long(1767139200000LL)}, nullptr, "putLong");
    CallVirtual(ed, "putBoolean",
                "(Ljava/lang/String;Z)Landroid/content/SharedPreferences$Editor;",
                {Str("accepted_eula"), DexValue::Int(1)}, nullptr, "putBoolean");
    CallVirtual(ed, "putFloat", "(Ljava/lang/String;F)Landroid/content/SharedPreferences$Editor;",
                {Str("gamma"), DexValue::Float(1.25f)}, nullptr, "putFloat");
    DexValue committed;
    if (CallVirtual(ed, "commit", "()Z", {}, &committed, "commit()")) {
        Check(committed.i == 1, "commit() reports success");
    }

    // ...and read through a SECOND instance, which is the whole point: an app relaunching
    // constructs a fresh one and must see what the first one stored. In-memory passes every
    // check made through the same object and fails here.
    std::printf("-- a second instance reads what the first wrote --\n");
    DexObject* reopened = NewPrefs(guestDir, "settings", "reopen SharedPreferencesImpl");
    if (reopened != nullptr) {
        DexValue s;
        if (CallVirtual(reopened, "getString",
                        "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
                        {Str("device_id"), Str("MISSING")}, &s, "getString(device_id)")) {
            Check(std::strcmp(Utf8Of(s), "6ba7b810-9dad-11d1-80b4-00c04fd430c8") == 0,
                  std::string("the stored device ID came back: ") + Utf8Of(s));
        }
        DexValue i;
        if (CallVirtual(reopened, "getInt", "(Ljava/lang/String;I)I",
                        {Str("launches"), DexValue::Int(-1)}, &i, "getInt")) {
            Check(i.i == 7, "an int survives the round trip");
        }
        DexValue l;
        if (CallVirtual(reopened, "getLong", "(Ljava/lang/String;J)J",
                        {Str("installed_at"), DexValue::Long(-1)}, &l, "getLong")) {
            Check(l.j == 1767139200000LL, "a long keeps all 64 bits");
        }
        DexValue b;
        if (CallVirtual(reopened, "getBoolean", "(Ljava/lang/String;Z)Z",
                        {Str("accepted_eula"), DexValue::Int(0)}, &b, "getBoolean")) {
            Check(b.i == 1, "a boolean survives the round trip");
        }
        DexValue f;
        if (CallVirtual(reopened, "getFloat", "(Ljava/lang/String;F)F",
                        {Str("gamma"), DexValue::Float(-1.0f)}, &f, "getFloat")) {
            Check(f.f > 1.24f && f.f < 1.26f, "a float survives the round trip");
        }
        DexValue missing;
        if (CallVirtual(reopened, "getString",
                        "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
                        {Str("never_set"), Str("fallback")}, &missing, "getString(absent)")) {
            Check(std::strcmp(Utf8Of(missing), "fallback") == 0,
                  "an absent key still returns the default");
        }
        DexValue has;
        if (CallVirtual(reopened, "contains", "(Ljava/lang/String;)Z",
                        {Str("device_id")}, &has, "contains")) {
            Check(has.i == 1, "contains() sees a loaded key");
        }
    }

    // Values that could forge a line boundary in the on-disk format. A key holding '=' or a
    // value holding a newline must come back intact — otherwise one setting can overwrite
    // or truncate another, and an app storing a JSON blob or a file path hits it.
    std::printf("-- keys and values that could break the file format --\n");
    {
        DexObject* tricky = NewPrefs(guestDir, "tricky", "new prefs(tricky)");
        if (tricky != nullptr) {
            DexValue e2;
            if (CallVirtual(tricky, "edit", "()Landroid/content/SharedPreferences$Editor;",
                            {}, &e2, "edit(tricky)")) {
                CallVirtual(e2.l, "putString",
                            "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/SharedPreferences$Editor;",
                            {Str("key=with=equals"), Str("line1\nline2")}, nullptr,
                            "putString(newline)");
                CallVirtual(e2.l, "putString",
                            "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/SharedPreferences$Editor;",
                            {Str("back\\slash"), Str("semi;colon")}, nullptr,
                            "putString(escapes)");
                CallVirtual(e2.l, "commit", "()Z", {}, nullptr, "commit(tricky)");
            }
            DexObject* reread = NewPrefs(guestDir, "tricky", "reopen prefs(tricky)");
            if (reread != nullptr) {
                DexValue v1, v2;
                if (CallVirtual(reread, "getString",
                                "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
                                {Str("key=with=equals"), Str("LOST")}, &v1, "getString(=)")) {
                    Check(std::strcmp(Utf8Of(v1), "line1\nline2") == 0,
                          "a value containing a newline round-trips");
                }
                if (CallVirtual(reread, "getString",
                                "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
                                {Str("back\\slash"), Str("LOST")}, &v2, "getString(\\)")) {
                    Check(std::strcmp(Utf8Of(v2), "semi;colon") == 0,
                          "a backslash in a key and a ';' in a value round-trip");
                }
            }
        }
    }

    // clear() applies BEFORE the puts in the same editor. That is the documented order, and
    // getting it wrong discards values the app put after calling clear().
    std::printf("-- clear() then put, in one editor --\n");
    {
        DexObject* p = NewPrefs(guestDir, "cleared", "new prefs(cleared)");
        if (p != nullptr) {
            DexValue e1;
            if (CallVirtual(p, "edit", "()Landroid/content/SharedPreferences$Editor;", {},
                            &e1, "edit(1)")) {
                CallVirtual(e1.l, "putString",
                            "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/SharedPreferences$Editor;",
                            {Str("old"), Str("value")}, nullptr, "putString(old)");
                CallVirtual(e1.l, "commit", "()Z", {}, nullptr, "commit(1)");
            }
            DexValue e2;
            if (CallVirtual(p, "edit", "()Landroid/content/SharedPreferences$Editor;", {},
                            &e2, "edit(2)")) {
                CallVirtual(e2.l, "clear", "()Landroid/content/SharedPreferences$Editor;",
                            {}, nullptr, "clear()");
                CallVirtual(e2.l, "putString",
                            "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/SharedPreferences$Editor;",
                            {Str("new"), Str("kept")}, nullptr, "putString(new)");
                CallVirtual(e2.l, "commit", "()Z", {}, nullptr, "commit(2)");
            }
            DexValue gone, kept;
            if (CallVirtual(p, "getString",
                            "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
                            {Str("old"), Str("ABSENT")}, &gone, "getString(old)")) {
                Check(std::strcmp(Utf8Of(gone), "ABSENT") == 0, "clear() removed the old key");
            }
            if (CallVirtual(p, "getString",
                            "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
                            {Str("new"), Str("LOST")}, &kept, "getString(new)")) {
                Check(std::strcmp(Utf8Of(kept), "kept") == 0,
                      "a put after clear() in the same editor is kept");
            }
        }
    }

    // remove(), and a null string value meaning removal.
    std::printf("-- remove() and a null value --\n");
    {
        DexObject* p = NewPrefs(guestDir, "removals", "new prefs(removals)");
        if (p != nullptr) {
            DexValue e1;
            if (CallVirtual(p, "edit", "()Landroid/content/SharedPreferences$Editor;", {},
                            &e1, "edit(rm)")) {
                CallVirtual(e1.l, "putString",
                            "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/SharedPreferences$Editor;",
                            {Str("a"), Str("1")}, nullptr, "putString(a)");
                CallVirtual(e1.l, "putString",
                            "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/SharedPreferences$Editor;",
                            {Str("b"), Str("2")}, nullptr, "putString(b)");
                CallVirtual(e1.l, "commit", "()Z", {}, nullptr, "commit(rm1)");
            }
            DexValue e2;
            if (CallVirtual(p, "edit", "()Landroid/content/SharedPreferences$Editor;", {},
                            &e2, "edit(rm2)")) {
                CallVirtual(e2.l, "remove", "(Ljava/lang/String;)Landroid/content/SharedPreferences$Editor;",
                            {Str("a")}, nullptr, "remove(a)");
                // A null value means remove, per the SharedPreferences contract.
                CallVirtual(e2.l, "putString",
                            "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/SharedPreferences$Editor;",
                            {Str("b"), DexValue::Ref(nullptr)}, nullptr, "putString(b, null)");
                CallVirtual(e2.l, "commit", "()Z", {}, nullptr, "commit(rm2)");
            }
            DexValue ha, hb;
            if (CallVirtual(p, "contains", "(Ljava/lang/String;)Z", {Str("a")}, &ha,
                            "contains(a)")) {
                Check(ha.i == 0, "remove() took the key out");
            }
            if (CallVirtual(p, "contains", "(Ljava/lang/String;)Z", {Str("b")}, &hb,
                            "contains(b)")) {
                Check(hb.i == 0, "a null value removes the key");
            }
        }
    }

    // getAll and the string-set methods — the four the interface was missing, so an app
    // holding a SharedPreferences-typed variable could not call them at all.
    std::printf("-- getAll and string sets --\n");
    {
        DexObject* p = NewPrefs(guestDir, "sets", "new prefs(sets)");
        if (p != nullptr) {
            DexObject* set = NewObject("Ljava/util/LinkedHashSet;", "()V", {},
                                       "new LinkedHashSet");
            if (set != nullptr) {
                CallVirtual(set, "add", "(Ljava/lang/Object;)Z", {Str("alpha")}, nullptr,
                            "set.add(alpha)");
                CallVirtual(set, "add", "(Ljava/lang/Object;)Z", {Str("beta")}, nullptr,
                            "set.add(beta)");
                DexValue e;
                if (CallVirtual(p, "edit", "()Landroid/content/SharedPreferences$Editor;",
                                {}, &e, "edit(sets)")) {
                    CallVirtual(e.l, "putStringSet",
                                "(Ljava/lang/String;Ljava/util/Set;)Landroid/content/SharedPreferences$Editor;",
                                {Str("tags"), DexValue::Ref(set)}, nullptr, "putStringSet");
                    CallVirtual(e.l, "commit", "()Z", {}, nullptr, "commit(sets)");
                }
                DexObject* reread = NewPrefs(guestDir, "sets", "reopen prefs(sets)");
                if (reread != nullptr) {
                    DexValue got;
                    if (CallVirtual(reread, "getStringSet",
                                    "(Ljava/lang/String;Ljava/util/Set;)Ljava/util/Set;",
                                    {Str("tags"), DexValue::Ref(nullptr)}, &got,
                                    "getStringSet")) {
                        Check(got.l != nullptr, "a persisted set comes back non-null");
                        if (got.l != nullptr) {
                            DexValue size;
                            if (CallVirtual(got.l, "size", "()I", {}, &size, "set.size")) {
                                Check(size.i == 2, "both members survived");
                            }
                            DexValue hasAlpha;
                            if (CallVirtual(got.l, "contains", "(Ljava/lang/Object;)Z",
                                            {Str("alpha")}, &hasAlpha, "set.contains")) {
                                Check(hasAlpha.i == 1, "a member is found by value");
                            }
                        }
                    }
                    // A null default is returned as null, not as an empty set: a caller
                    // that passed null expects null back.
                    DexValue absent;
                    if (CallVirtual(reread, "getStringSet",
                                    "(Ljava/lang/String;Ljava/util/Set;)Ljava/util/Set;",
                                    {Str("no_such_set"), DexValue::Ref(nullptr)}, &absent,
                                    "getStringSet(absent)")) {
                        Check(absent.l == nullptr, "a null default comes back as null");
                    }
                    DexValue all;
                    if (CallVirtual(reread, "getAll", "()Ljava/util/Map;", {}, &all,
                                    "getAll")) {
                        Check(all.l != nullptr, "getAll returns a map");
                        if (all.l != nullptr) {
                            DexValue size;
                            if (CallVirtual(all.l, "size", "()I", {}, &size, "map.size")) {
                                Check(size.i == 1, "getAll sees the stored key");
                            }
                        }
                    }
                }
            }
        }
    }

    // A memory-only instance must still work — the no-arg constructor has nowhere to write,
    // and it must not fault trying.
    std::printf("-- a memory-only instance still works --\n");
    {
        DexObject* mem = NewObject("Landroid/content/SharedPreferencesImpl;",
                                   "(Ljava/lang/String;)V", {Str("memory")},
                                   "new prefs(no directory)");
        if (mem != nullptr) {
            DexValue e;
            if (CallVirtual(mem, "edit", "()Landroid/content/SharedPreferences$Editor;", {},
                            &e, "edit(mem)")) {
                CallVirtual(e.l, "putString",
                            "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/SharedPreferences$Editor;",
                            {Str("k"), Str("v")}, nullptr, "putString(mem)");
                CallVirtual(e.l, "commit", "()Z", {}, nullptr, "commit(mem)");
            }
            DexValue v;
            if (CallVirtual(mem, "getString",
                            "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
                            {Str("k"), Str("LOST")}, &v, "getString(mem)")) {
                Check(std::strcmp(Utf8Of(v), "v") == 0,
                      "an in-memory store works within its own lifetime");
            }
        }
    }
}

// The prefs file must be a real file on the host, under android_root. Checking only through
// Java would pass even if both sides were consistently wrong about where it lives.
void TestPrefsFileOnDisk(const std::string& androidRoot) {
    std::printf("-- the prefs file is where it should be --\n");
    const std::string path =
        androidRoot + "/data/data/com.kudroid.test/shared_prefs/settings.prefs";
    Check(std::filesystem::exists(path),
          "settings.prefs exists under android_root/data/data/<pkg>/shared_prefs");
    if (std::filesystem::exists(path)) {
        std::ifstream in(path);
        const std::string text((std::istreambuf_iterator<char>(in)), {});
        Check(text.find("device_id") != std::string::npos,
              "the file contains the key that was written");
        Check(text.find("6ba7b810-9dad-11d1-80b4-00c04fd430c8") != std::string::npos,
              "the file contains the value that was written");
        // The temporary file used for the atomic replace must not be left behind.
        Check(!std::filesystem::exists(path + ".tmp"),
              "the temporary file was renamed away, not left on disk");
    }
}

// ── the Context path an app actually uses ───────────────────────────────────
//
// Everything above constructs SharedPreferencesImpl directly. An app does not: it calls
// context.getSharedPreferences(name, mode), and that used to return a NEW instance on every
// call — two independent stores, so a value written through one was absent from the other.
// Even a write-then-read inside a single run produced the default.
//
// Identity is the property to check, not just the value. A change listener is registered on
// one object and fired by the writer's object, so two instances mean the callback never
// arrives even though both eventually agree on disk.
void TestContextReturnsOneInstance() {
    std::printf("-- Context.getSharedPreferences returns the same instance --\n");

    DexObject* ctx = NewObject("Landroid/app/ApplicationContext;", "(Ljava/lang/String;)V",
                               {Str("com.kudroid.test")}, "new ApplicationContext");
    if (ctx == nullptr) return;

    DexValue first, second;
    if (!CallVirtual(ctx, "getSharedPreferences",
                     "(Ljava/lang/String;I)Landroid/content/SharedPreferences;",
                     {Str("ctx_prefs"), DexValue::Int(0)}, &first, "getSharedPreferences 1")) {
        return;
    }
    if (!CallVirtual(ctx, "getSharedPreferences",
                     "(Ljava/lang/String;I)Landroid/content/SharedPreferences;",
                     {Str("ctx_prefs"), DexValue::Int(0)}, &second, "getSharedPreferences 2")) {
        return;
    }
    Check(first.l != nullptr && first.l == second.l,
          "two calls with the same name return the identical object");

    // A different name must NOT share the instance, or every store would alias.
    DexValue other;
    if (CallVirtual(ctx, "getSharedPreferences",
                    "(Ljava/lang/String;I)Landroid/content/SharedPreferences;",
                    {Str("other_prefs"), DexValue::Int(0)}, &other, "getSharedPreferences 3")) {
        Check(other.l != nullptr && other.l != first.l,
              "a different name returns a different object");
    }

    // Write through the first handle and read through the second. With a fresh instance per
    // call this returns the default even though nothing was wrong with persistence.
    if (first.l != nullptr) {
        DexValue e;
        if (CallVirtual(first.l, "edit", "()Landroid/content/SharedPreferences$Editor;", {},
                        &e, "edit via context")) {
            CallVirtual(e.l, "putString",
                        "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/SharedPreferences$Editor;",
                        {Str("k"), Str("written-through-first")}, nullptr, "putString");
            CallVirtual(e.l, "commit", "()Z", {}, nullptr, "commit");
        }
        DexValue got;
        if (CallVirtual(second.l, "getString",
                        "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
                        {Str("k"), Str("LOST")}, &got, "getString via second handle")) {
            Check(std::strcmp(Utf8Of(got), "written-through-first") == 0,
                  "a value written through one handle is visible through the other");
        }
    }

    // PreferenceManager must reach the same store as getSharedPreferences with the name it
    // uses. Minecraft reads its device ID back through both paths, so a mismatch here loses
    // the value while every individual piece looks correct.
    std::printf("-- PreferenceManager shares the store with getSharedPreferences --\n");
    {
        DexValue viaManager;
        if (CallStatic("Landroid/preference/PreferenceManager;", "getDefaultSharedPreferences",
                       "(Landroid/content/Context;)Landroid/content/SharedPreferences;",
                       {DexValue::Ref(ctx)}, &viaManager, "getDefaultSharedPreferences")) {
            Check(viaManager.l != nullptr, "getDefaultSharedPreferences returns a store");

            DexValue name;
            if (CallStatic("Landroid/preference/PreferenceManager;",
                           "getDefaultSharedPreferencesName",
                           "(Landroid/content/Context;)Ljava/lang/String;",
                           {DexValue::Ref(ctx)}, &name, "getDefaultSharedPreferencesName")) {
                Check(std::strcmp(Utf8Of(name), "com.kudroid.test_preferences") == 0,
                      std::string("the default name matches Android's: ") + Utf8Of(name));

                DexValue direct;
                if (CallVirtual(ctx, "getSharedPreferences",
                                "(Ljava/lang/String;I)Landroid/content/SharedPreferences;",
                                {name, DexValue::Int(0)}, &direct, "getSharedPreferences(name)")) {
                    Check(direct.l == viaManager.l,
                          "PreferenceManager and getSharedPreferences give the same object");
                }
            }

            // The round trip an app performs: store a generated ID, read it back.
            if (viaManager.l != nullptr) {
                DexValue e;
                if (CallVirtual(viaManager.l, "edit",
                                "()Landroid/content/SharedPreferences$Editor;", {}, &e,
                                "edit via manager")) {
                    CallVirtual(e.l, "putString",
                                "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/SharedPreferences$Editor;",
                                {Str("legacy_device_id"),
                                 Str("6ba7b810-9dad-11d1-80b4-00c04fd430c8")},
                                nullptr, "putString(device id)");
                    CallVirtual(e.l, "commit", "()Z", {}, nullptr, "commit via manager");
                }
                DexValue back;
                if (CallVirtual(viaManager.l, "getString",
                                "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
                                {Str("legacy_device_id"), DexValue::Ref(nullptr)}, &back,
                                "getString(device id)")) {
                    Check(back.l != nullptr &&
                              std::strcmp(Utf8Of(back),
                                          "6ba7b810-9dad-11d1-80b4-00c04fd430c8") == 0,
                          "a device ID stored through PreferenceManager reads back");
                }
            }
        }
    }
}

}  // namespace

int main() {
    // HOME first: the remapper derives android_root from it on first use, and the
    // singleton is constructed the moment anything touches the VFS.
    const std::filesystem::path home =
        std::filesystem::temp_directory_path() /
        ("kudroid_prefs_test_" + std::to_string(::getpid()));
    std::filesystem::remove_all(home);
    std::filesystem::create_directories(home / "Documents");
    ::setenv("HOME", home.string().c_str(), 1);

    std::printf("=== KuART Java file I/O and SharedPreferences ===\n");

    DexClassLinker linker;
    std::string error;
    if (!linker.AddDexFile(g_framework_dex_bytes, g_framework_dex_size, "framework.dex",
                           &error)) {
        std::printf("  FAIL AddDexFile(framework.dex): %s\n=== FAILED ===\n", error.c_str());
        return 1;
    }

    Interpreter interp(&linker);
    DexJniEnv jni(&linker, &interp);
    interp.set_jni_env(&jni);
    g_linker = &linker;
    g_interp = &interp;
    interp.set_instruction_limit(2000ull * 1000ull * 1000ull);

    auto& remapper = kudroid::VFSPathRemapper::getInstance();
    const std::string androidRoot = remapper.androidRoot();
    std::printf("       android_root: %s\n", androidRoot.c_str());

    // The classes must be real, not auto-stubs. An auto-stub answers every call with zero,
    // which for a getter is indistinguishable from "not stored".
    {
        const char* wanted[] = {
            "Ljava/io/FileInputStream;", "Ljava/io/FileOutputStream;", "Ljava/io/File;",
            "Landroid/content/SharedPreferencesImpl;",
            "Landroid/preference/PreferenceManager;",
        };
        for (const char* d : wanted) {
            DexClass* k = linker.FindClass(d);
            Check(k != nullptr && !k->is_stub, std::string("present and not a stub: ") + d);
        }
    }

    TestFileWriteThenRead(androidRoot);
    TestPreferencesPersist(androidRoot);
    TestPrefsFileOnDisk(androidRoot);
    TestContextReturnsOneInstance();

    std::filesystem::remove_all(home);

    std::printf("=== %d checks, %d failures ===\n", g_checks, g_failures);
    if (g_failures != 0) {
        std::printf("=== FAILED ===\n");
        return 1;
    }
    std::printf("=== PASSED ===\n");
    return 0;
}
