// Integration test: run the real framework.dex inside KuART and drive the Java that
// was just added — java.util.regex, java.text.SimpleDateFormat, java.util.Calendar.
//
// Compiling is not evidence. These classes are large, mutually dependent, and reach
// deep into the interpreter (filled-new-array in enum $values(), string natives,
// ListResourceBundle lookups, exception tables). Executing them here is what proves
// KuART can actually run them on device.
//
// Methods are invoked directly through the linker rather than from synthesised
// bytecode: the point is to exercise the framework code, not the call sequence.
#include "kudroid/framework_dex_bytes.h"
#include "kudroid/kuart/DexClassLinker.h"
#include "kudroid/kuart/DexJniEnv.h"
#include "kudroid/kuart/DexObject.h"
#include "kudroid/kuart/DexString.h"
#include "kudroid/kuart/Interpreter.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void Check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "  OK  " : "  FAIL", what.c_str());
    if (!ok) ++g_failures;
}

using kudroid::kuart::DexClass;
using kudroid::kuart::DexClassLinker;
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

// Call a static method, reporting any pending exception as a failure.
bool CallStatic(const char* descriptor, const char* name, const char* sig,
                std::vector<DexValue> args, DexValue* out, const char* what) {
    DexClass* klass = g_linker->FindClass(descriptor);
    if (klass == nullptr || klass->is_stub) {
        std::printf("  FAIL %s: class %s not in framework.dex\n", what, descriptor);
        ++g_failures;
        return false;
    }
    g_interp->ClearPendingException();
    if (!g_interp->EnsureInitialized(klass)) {
        std::printf("  FAIL %s: <clinit> of %s failed: %s\n", what, descriptor,
                    g_interp->last_error().c_str());
        ++g_failures;
        g_interp->ClearPendingException();
        return false;
    }
    DexMethod* m = klass->FindDirectMethod(name, sig);
    if (m == nullptr) m = klass->FindVirtualMethod(name, sig);
    if (m == nullptr) {
        std::printf("  FAIL %s: no method %s%s\n", what, name, sig);
        ++g_failures;
        return false;
    }
    const DexValue r = g_interp->Execute(m, args.data(), args.size());
    if (g_interp->HasPendingException()) {
        std::printf("  FAIL %s threw: %s\n", what, g_interp->last_error().c_str());
        ++g_failures;
        g_interp->ClearPendingException();
        return false;
    }
    if (out != nullptr) *out = r;
    return true;
}

// Call an instance method on `receiver`, dispatching against its real class.
bool CallVirtual(DexObject* receiver, const char* name, const char* sig,
                 std::vector<DexValue> args, DexValue* out, const char* what) {
    if (receiver == nullptr || receiver->clazz == nullptr) {
        std::printf("  FAIL %s: null receiver\n", what);
        ++g_failures;
        return false;
    }
    DexMethod* m = receiver->clazz->FindVirtualMethod(name, sig);
    if (m == nullptr) m = receiver->clazz->FindDirectMethod(name, sig);
    if (m == nullptr) {
        std::printf("  FAIL %s: no method %s%s on %s\n", what, name, sig,
                    receiver->clazz->PrettyName().c_str());
        ++g_failures;
        return false;
    }
    std::vector<DexValue> full;
    full.push_back(DexValue::Ref(receiver));
    for (const DexValue& v : args) full.push_back(v);

    g_interp->ClearPendingException();
    const DexValue r = g_interp->Execute(m, full.data(), full.size());
    if (g_interp->HasPendingException()) {
        std::printf("  FAIL %s threw: %s\n", what, g_interp->last_error().c_str());
        ++g_failures;
        g_interp->ClearPendingException();
        return false;
    }
    if (out != nullptr) *out = r;
    return true;
}

// new <descriptor>(args) using the given constructor signature.
DexObject* NewObject(const char* descriptor, const char* ctorSig,
                     std::vector<DexValue> args, const char* what) {
    DexClass* klass = g_linker->FindClass(descriptor);
    if (klass == nullptr || klass->is_stub) {
        std::printf("  FAIL %s: class %s not in framework.dex\n", what, descriptor);
        ++g_failures;
        return nullptr;
    }
    g_interp->ClearPendingException();
    if (!g_interp->EnsureInitialized(klass)) {
        std::printf("  FAIL %s: <clinit> failed: %s\n", what,
                    g_interp->last_error().c_str());
        ++g_failures;
        g_interp->ClearPendingException();
        return nullptr;
    }
    DexObject* obj = g_linker->AllocObject(klass);
    if (obj == nullptr) {
        std::printf("  FAIL %s: AllocObject failed\n", what);
        ++g_failures;
        return nullptr;
    }
    DexMethod* ctor = klass->FindDirectMethod("<init>", ctorSig);
    if (ctor == nullptr) {
        std::printf("  FAIL %s: no <init>%s\n", what, ctorSig);
        ++g_failures;
        return nullptr;
    }
    std::vector<DexValue> full;
    full.push_back(DexValue::Ref(obj));
    for (const DexValue& v : args) full.push_back(v);

    g_interp->Execute(ctor, full.data(), full.size());
    if (g_interp->HasPendingException()) {
        std::printf("  FAIL %s ctor threw: %s\n", what, g_interp->last_error().c_str());
        ++g_failures;
        g_interp->ClearPendingException();
        return nullptr;
    }
    return obj;
}

}  // namespace

int main() {
    std::printf("=== KuART framework integration: regex + java.text ===\n");

    DexClassLinker linker;
    std::string error;
    if (!linker.AddDexFile(g_framework_dex_bytes, g_framework_dex_size, "framework.dex",
                           &error)) {
        std::printf("  FAIL AddDexFile(framework.dex): %s\n=== FAILED ===\n",
                    error.c_str());
        return 1;
    }
    std::printf("framework.dex: %zu bytes\n", g_framework_dex_size);

    Interpreter interp(&linker);
    DexJniEnv jni(&linker, &interp);
    interp.set_jni_env(&jni);
    g_linker = &linker;
    g_interp = &interp;

    // The interpreter's default budget is generous, but regex compilation is the most
    // instruction-hungry Java KuDroid runs; a low limit here would look like a hang.
    interp.set_instruction_limit(2000ull * 1000ull * 1000ull);

    // ── the classes are present and real, not auto-stubs ──
    {
        const char* wanted[] = {
            "Ljava/util/regex/Pattern;", "Ljava/util/regex/Matcher;",
            "Ljava/text/SimpleDateFormat;", "Ljava/text/DateFormat;",
            "Ljava/text/NumberFormat;", "Ljava/util/Calendar;",
            "Ljava/util/GregorianCalendar;", "Ljava/util/TimeZone;",
            "Ljava/util/ListResourceBundle;",
        };
        for (const char* d : wanted) {
            DexClass* k = linker.FindClass(d);
            Check(k != nullptr && !k->is_stub,
                  std::string("present and not a stub: ") + d);
        }
    }

    // ── java.util.regex ──
    // Pattern.matches(regex, input) compiles a pattern and runs the matcher, so a
    // single call already covers the Lexer, the AbstractCharClass tables and the
    // node graph.
    {
        DexValue r;
        if (CallStatic("Ljava/util/regex/Pattern;", "matches",
                       "(Ljava/lang/String;Ljava/lang/CharSequence;)Z",
                       {Str("[0-9]+"), Str("12345")}, &r, "Pattern.matches digits")) {
            Check(r.i == 1, "Pattern.matches(\"[0-9]+\", \"12345\") is true");
        }
        if (CallStatic("Ljava/util/regex/Pattern;", "matches",
                       "(Ljava/lang/String;Ljava/lang/CharSequence;)Z",
                       {Str("[0-9]+"), Str("12a45")}, &r,
                       "Pattern.matches non-digits")) {
            Check(r.i == 0, "Pattern.matches(\"[0-9]+\", \"12a45\") is false");
        }
        // Anchors, alternation and quantifiers in one pattern.
        if (CallStatic("Ljava/util/regex/Pattern;", "matches",
                       "(Ljava/lang/String;Ljava/lang/CharSequence;)Z",
                       {Str("^(foo|bar)-\\d{2,3}$"), Str("bar-123")}, &r,
                       "Pattern.matches alternation")) {
            Check(r.i == 1, "Pattern.matches(\"^(foo|bar)-\\\\d{2,3}$\", \"bar-123\")");
        }
        // A predefined character class, which goes through ListResourceBundle.
        if (CallStatic("Ljava/util/regex/Pattern;", "matches",
                       "(Ljava/lang/String;Ljava/lang/CharSequence;)Z",
                       {Str("\\p{Alpha}+"), Str("abcXYZ")}, &r,
                       "Pattern.matches \\p{Alpha}")) {
            Check(r.i == 1, "Pattern.matches(\"\\\\p{Alpha}+\", \"abcXYZ\")");
        }
    }

    // String.matches / replaceAll / split now route to the engine. Before this they
    // faked it: matches() compared for equality and replaceAll() did a literal
    // replace, returning wrong answers with no error.
    {
        DexValue r;
        DexObject* s = reinterpret_cast<DexObject*>(linker.NewString("a1b22c333"));
        if (CallVirtual(s, "matches", "(Ljava/lang/String;)Z",
                        {Str("([a-z]\\d+)+")}, &r, "String.matches")) {
            Check(r.i == 1, "\"a1b22c333\".matches(\"([a-z]\\\\d+)+\") is true");
        }
        // The old implementation returned true here because it compared for equality.
        if (CallVirtual(s, "matches", "(Ljava/lang/String;)Z",
                        {Str("a1b22c333")}, &r, "String.matches literal-as-regex")) {
            Check(r.i == 1, "a literal pattern still matches itself");
        }
        if (CallVirtual(s, "matches", "(Ljava/lang/String;)Z",
                        {Str("\\d+")}, &r, "String.matches negative")) {
            Check(r.i == 0, "\"a1b22c333\".matches(\"\\\\d+\") is false");
        }

        if (CallVirtual(s, "replaceAll", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
                        {Str("\\d+"), Str("#")}, &r, "String.replaceAll")) {
            Check(std::strcmp(Utf8Of(r), "a#b#c#") == 0,
                  std::string("replaceAll(\"\\\\d+\", \"#\") == \"a#b#c#\", got \"") +
                      Utf8Of(r) + "\"");
        }
        if (CallVirtual(s, "replaceFirst", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
                        {Str("\\d+"), Str("#")}, &r, "String.replaceFirst")) {
            Check(std::strcmp(Utf8Of(r), "a#b22c333") == 0,
                  std::string("replaceFirst == \"a#b22c333\", got \"") + Utf8Of(r) + "\"");
        }
    }

    // split() keeps a fast path for a plain one-character separator and uses the
    // engine otherwise; both need checking.
    {
        DexValue r;
        DexObject* csv = reinterpret_cast<DexObject*>(linker.NewString("a,b,c"));
        if (CallVirtual(csv, "split", "(Ljava/lang/String;)[Ljava/lang/String;",
                        {Str(",")}, &r, "String.split literal")) {
            auto* arr = static_cast<kudroid::kuart::DexArray*>(r.l);
            Check(arr != nullptr && arr->length == 3, "split(\",\") gives 3 parts");
            if (arr != nullptr && arr->length == 3) {
                Check(std::strcmp(reinterpret_cast<DexString*>(
                                      arr->Get<DexObject*>(1))->utf8, "b") == 0,
                      "split(\",\")[1] == \"b\"");
            }
        }
        DexObject* spaced = reinterpret_cast<DexObject*>(linker.NewString("a1b22c"));
        if (CallVirtual(spaced, "split", "(Ljava/lang/String;)[Ljava/lang/String;",
                        {Str("\\d+")}, &r, "String.split regex")) {
            auto* arr = static_cast<kudroid::kuart::DexArray*>(r.l);
            Check(arr != nullptr && arr->length == 3,
                  "split(\"\\\\d+\") gives 3 parts (regex path)");
        }
    }

    // ── java.util.Calendar / GregorianCalendar ──
    // A fixed instant, formatted in UTC so the host's zone cannot change the answer.
    // 1234567890000 ms = 2009-02-13 23:31:30 UTC.
    {
        DexValue utcZone;
        if (!CallStatic("Ljava/util/TimeZone;", "getTimeZone",
                        "(Ljava/lang/String;)Ljava/util/TimeZone;", {Str("UTC")},
                        &utcZone, "TimeZone.getTimeZone(UTC)")) {
            utcZone = DexValue();
        }

        DexObject* cal = NewObject("Ljava/util/GregorianCalendar;",
                                   "(Ljava/util/TimeZone;)V", {utcZone},
                                   "new GregorianCalendar(UTC)");
        if (cal != nullptr) {
            CallVirtual(cal, "setTimeInMillis", "(J)V",
                        {DexValue::Long(1234567890000LL)}, nullptr,
                        "Calendar.setTimeInMillis");

            DexValue y, mo, d, h, mi, sec, dow;
            const bool ok =
                CallVirtual(cal, "get", "(I)I", {DexValue::Int(1)}, &y, "get(YEAR)") &&
                CallVirtual(cal, "get", "(I)I", {DexValue::Int(2)}, &mo, "get(MONTH)") &&
                CallVirtual(cal, "get", "(I)I", {DexValue::Int(5)}, &d, "get(DAY)") &&
                CallVirtual(cal, "get", "(I)I", {DexValue::Int(11)}, &h, "get(HOUR_OF_DAY)") &&
                CallVirtual(cal, "get", "(I)I", {DexValue::Int(12)}, &mi, "get(MINUTE)") &&
                CallVirtual(cal, "get", "(I)I", {DexValue::Int(13)}, &sec, "get(SECOND)") &&
                CallVirtual(cal, "get", "(I)I", {DexValue::Int(7)}, &dow, "get(DAY_OF_WEEK)");
            if (ok) {
                Check(y.i == 2009, std::string("YEAR == 2009, got ") + std::to_string(y.i));
                Check(mo.i == 1, std::string("MONTH == 1 (February), got ") +
                                     std::to_string(mo.i));
                Check(d.i == 13, std::string("DAY_OF_MONTH == 13, got ") +
                                     std::to_string(d.i));
                Check(h.i == 23, std::string("HOUR_OF_DAY == 23, got ") +
                                     std::to_string(h.i));
                Check(mi.i == 31, std::string("MINUTE == 31, got ") + std::to_string(mi.i));
                Check(sec.i == 30, std::string("SECOND == 30, got ") + std::to_string(sec.i));
                // 2009-02-13 was a Friday; Calendar.FRIDAY == 6.
                Check(dow.i == 6, std::string("DAY_OF_WEEK == FRIDAY(6), got ") +
                                      std::to_string(dow.i));
            }

            // add() must carry across a month boundary, and clamp the day when the
            // target month is shorter.
            DexObject* cal2 = NewObject("Ljava/util/GregorianCalendar;",
                                        "(III)V",
                                        {DexValue::Int(2020), DexValue::Int(0),
                                         DexValue::Int(31)},
                                        "new GregorianCalendar(2020, JAN, 31)");
            if (cal2 != nullptr) {
                CallVirtual(cal2, "add", "(II)V",
                            {DexValue::Int(2), DexValue::Int(1)}, nullptr,
                            "Calendar.add(MONTH, 1)");
                DexValue y2, m2, d2;
                if (CallVirtual(cal2, "get", "(I)I", {DexValue::Int(1)}, &y2, "get(YEAR)") &&
                    CallVirtual(cal2, "get", "(I)I", {DexValue::Int(2)}, &m2, "get(MONTH)") &&
                    CallVirtual(cal2, "get", "(I)I", {DexValue::Int(5)}, &d2, "get(DAY)")) {
                    Check(y2.i == 2020 && m2.i == 1 && d2.i == 29,
                          std::string("Jan 31 + 1 month == Feb 29 2020 (leap), got ") +
                              std::to_string(y2.i) + "-" + std::to_string(m2.i + 1) + "-" +
                              std::to_string(d2.i));
                }
            }
        }
    }

    // ── java.text.SimpleDateFormat ──
    {
        DexObject* fmt = NewObject("Ljava/text/SimpleDateFormat;",
                                   "(Ljava/lang/String;)V",
                                   {Str("yyyy-MM-dd HH:mm:ss")},
                                   "new SimpleDateFormat(\"yyyy-MM-dd HH:mm:ss\")");
        if (fmt != nullptr) {
            DexValue utcZone;
            if (CallStatic("Ljava/util/TimeZone;", "getTimeZone",
                           "(Ljava/lang/String;)Ljava/util/TimeZone;", {Str("UTC")},
                           &utcZone, "TimeZone.getTimeZone(UTC)")) {
                CallVirtual(fmt, "setTimeZone", "(Ljava/util/TimeZone;)V", {utcZone},
                            nullptr, "DateFormat.setTimeZone(UTC)");
            }

            DexObject* date = NewObject("Ljava/util/Date;", "(J)V",
                                        {DexValue::Long(1234567890000LL)},
                                        "new Date(1234567890000)");
            if (date != nullptr) {
                DexValue out;
                if (CallVirtual(fmt, "format", "(Ljava/util/Date;)Ljava/lang/String;",
                                {DexValue::Ref(date)}, &out, "SimpleDateFormat.format")) {
                    Check(std::strcmp(Utf8Of(out), "2009-02-13 23:31:30") == 0,
                          std::string("format == \"2009-02-13 23:31:30\", got \"") +
                              Utf8Of(out) + "\"");
                }
            }

            // Round-trip: parse the text back and confirm the instant survives.
            DexValue parsed;
            if (CallVirtual(fmt, "parse", "(Ljava/lang/String;)Ljava/util/Date;",
                            {Str("2009-02-13 23:31:30")}, &parsed,
                            "SimpleDateFormat.parse")) {
                if (parsed.l != nullptr) {
                    DexValue millis;
                    if (CallVirtual(parsed.l, "getTime", "()J", {}, &millis,
                                    "Date.getTime")) {
                        Check(millis.j == 1234567890000LL,
                              std::string("parse round-trips to 1234567890000, got ") +
                                  std::to_string(millis.j));
                    }
                }
            }
        }

        // Named month and weekday come from DateFormatSymbols.
        DexObject* fmt2 = NewObject("Ljava/text/SimpleDateFormat;",
                                    "(Ljava/lang/String;)V", {Str("EEE, d MMM yyyy")},
                                    "new SimpleDateFormat(\"EEE, d MMM yyyy\")");
        if (fmt2 != nullptr) {
            DexValue utcZone;
            if (CallStatic("Ljava/util/TimeZone;", "getTimeZone",
                           "(Ljava/lang/String;)Ljava/util/TimeZone;", {Str("UTC")},
                           &utcZone, "TimeZone.getTimeZone(UTC)")) {
                CallVirtual(fmt2, "setTimeZone", "(Ljava/util/TimeZone;)V", {utcZone},
                            nullptr, "setTimeZone");
            }
            DexObject* date = NewObject("Ljava/util/Date;", "(J)V",
                                        {DexValue::Long(1234567890000LL)},
                                        "new Date");
            if (date != nullptr) {
                DexValue out;
                if (CallVirtual(fmt2, "format", "(Ljava/util/Date;)Ljava/lang/String;",
                                {DexValue::Ref(date)}, &out, "format with names")) {
                    Check(std::strcmp(Utf8Of(out), "Fri, 13 Feb 2009") == 0,
                          std::string("format == \"Fri, 13 Feb 2009\", got \"") +
                              Utf8Of(out) + "\"");
                }
            }
        }
    }

    // ── java.text.NumberFormat ──
    // SimpleDateFormat relies on this for zero-padded fields, so a regression here
    // shows up as malformed dates.
    {
        DexValue nf;
        if (CallStatic("Ljava/text/NumberFormat;", "getIntegerInstance",
                       "()Ljava/text/NumberFormat;", {}, &nf,
                       "NumberFormat.getIntegerInstance")) {
            if (nf.l != nullptr) {
                DexValue out;
                if (CallVirtual(nf.l, "format", "(J)Ljava/lang/String;",
                                {DexValue::Long(1234567)}, &out, "NumberFormat.format")) {
                    Check(std::strcmp(Utf8Of(out), "1,234,567") == 0,
                          std::string("format(1234567) == \"1,234,567\", got \"") +
                              Utf8Of(out) + "\"");
                }
            }
        }
    }

    // ── app bootstrap: AppComponentFactory + Application + package-derived paths ──
    //
    // Android runs the declared factory and the Application before any component, so
    // their static initialisers happen before all app code. KuDroid used to skip
    // both, which left every app whose setup lives there running with empty state
    // and failing far away from the cause. None of this is app-specific: the names
    // come from the manifest and the paths are derived from the package.
    {
        DexClass* factory = linker.FindClass("Landroid/app/AppComponentFactory;");
        Check(factory != nullptr && !factory->is_stub,
              "android.app.AppComponentFactory present and not a stub");

        // The factory instantiates components by name. An app subclass overrides
        // these and calls through super, so the base implementation has to work.
        if (factory != nullptr && !factory->is_stub) {
            Check(factory->FindVirtualMethod(
                      "instantiateApplication",
                      "(Ljava/lang/ClassLoader;Ljava/lang/String;)Landroid/app/Application;") != nullptr,
                  "AppComponentFactory.instantiateApplication exists");
            Check(factory->FindVirtualMethod(
                      "instantiateActivity",
                      "(Ljava/lang/ClassLoader;Ljava/lang/String;Landroid/content/Intent;)"
                      "Landroid/app/Activity;") != nullptr,
                  "AppComponentFactory.instantiateActivity exists");

            DexObject* f = NewObject("Landroid/app/AppComponentFactory;", "()V", {},
                                     "new AppComponentFactory");
            if (f != nullptr) {
                DexValue app;
                if (CallVirtual(f, "instantiateApplication",
                                "(Ljava/lang/ClassLoader;Ljava/lang/String;)"
                                "Landroid/app/Application;",
                                {DexValue::Ref(nullptr), Str("android.app.Application")},
                                &app, "instantiateApplication(Application)")) {
                    Check(app.l != nullptr && app.l->clazz != nullptr &&
                              app.l->clazz->PrettyName() == "android.app.Application",
                          "factory built an android.app.Application");
                }
            }
        }

        // ActivityThread must expose the statics native code reaches the Context
        // through: currentActivityThread().getApplication() is the standard JNI way
        // for an SDK that never sees an Activity to obtain one.
        DexClass* at = linker.FindClass("Landroid/app/ActivityThread;");
        Check(at != nullptr && !at->is_stub, "android.app.ActivityThread present");
        if (at != nullptr && !at->is_stub) {
            Check(at->FindDirectMethod("currentActivityThread",
                                       "()Landroid/app/ActivityThread;") != nullptr,
                  "ActivityThread.currentActivityThread() exists");
            Check(at->FindDirectMethod("currentApplication",
                                       "()Landroid/app/Application;") != nullptr,
                  "ActivityThread.currentApplication() exists");
            Check(at->FindVirtualMethod("getApplication",
                                        "()Landroid/app/Application;") != nullptr,
                  "ActivityThread.getApplication() exists");
        }

        // Paths must follow the manifest's package. ContextWrapper falls back to a
        // fixed "com.kudroid.app" when it has no base context, so anything comparing
        // getPackageName() with its own identity — license checks, provider
        // authorities, crash reporters — used to see the wrong app.
        DexObject* ctx = NewObject("Landroid/app/ApplicationContext;",
                                   "(Ljava/lang/String;)V", {Str("com.example.probe")},
                                   "new ApplicationContext(pkg)");
        if (ctx != nullptr) {
            DexValue pkg;
            if (CallVirtual(ctx, "getPackageName", "()Ljava/lang/String;", {}, &pkg,
                            "getPackageName")) {
                Check(std::strcmp(Utf8Of(pkg), "com.example.probe") == 0,
                      std::string("getPackageName() == manifest package, got \"") +
                          Utf8Of(pkg) + "\"");
            }
            DexValue dir;
            if (CallVirtual(ctx, "getFilesDir", "()Ljava/io/File;", {}, &dir,
                            "getFilesDir") &&
                dir.l != nullptr) {
                DexValue path;
                if (CallVirtual(dir.l, "getPath", "()Ljava/lang/String;", {}, &path,
                                "File.getPath")) {
                    Check(std::strcmp(Utf8Of(path),
                                      "/data/data/com.example.probe/files") == 0,
                          std::string("getFilesDir() derived from the package, got \"") +
                              Utf8Of(path) + "\"");
                }
            }
        }
    }

    std::printf("  executed %llu instructions\n",
                static_cast<unsigned long long>(interp.instructions_executed()));

    if (g_failures == 0) {
        std::printf("=== KuART framework integration test PASSED ===\n");
        return 0;
    }
    std::printf("=== KuART framework integration test FAILED (%d errors) ===\n",
                g_failures);
    return 1;
}
