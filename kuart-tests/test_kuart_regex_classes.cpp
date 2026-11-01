#include "kudroid/framework_dex_bytes.h"
#include "kudroid/DeviceProfile.h"
#include "kudroid/kudroid_bridge.h"
#include "kudroid/kuart/DexClassLinker.h"
#include "kudroid/kuart/DexJniEnv.h"
#include "kudroid/kuart/DexObject.h"
#include "kudroid/kuart/DexString.h"
#include "kudroid/kuart/Interpreter.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

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


DexObject* Matcher(const char* pattern, const std::string& input, int flags = 0) {
    DexValue compiled, matcher;
    if (!CallStatic("Ljava/util/regex/Pattern;", "compile",
                    "(Ljava/lang/String;I)Ljava/util/regex/Pattern;",
                    {Str(pattern), DexValue::Int(flags)}, &compiled, "compile")) return nullptr;
    if (!CallVirtual(compiled.l, "matcher", "(Ljava/lang/CharSequence;)Ljava/util/regex/Matcher;",
                     {Str(input.c_str())}, &matcher, "matcher")) return nullptr;
    return matcher.l;
}
void Group(DexObject* matcher, int index, const std::string& expected) {
    DexValue result;
    if (CallVirtual(matcher, "group", "(I)Ljava/lang/String;", {DexValue::Int(index)}, &result, "group"))
        Check(Utf8Of(result) == expected, "capture " + std::to_string(index));
}
void Matches(const char* pattern, const std::string& input, bool expected, int flags = 0) {
    DexValue result;
    if (auto* matcher = Matcher(pattern, input, flags))
        if (CallVirtual(matcher, "matches", "()Z", {}, &result, pattern))
            Check(bool(result.i) == expected, pattern);
}
int main() {
    DexClassLinker linker;
    std::string error;
    if (!linker.AddDexFile(g_framework_dex_bytes, g_framework_dex_size, "framework.dex", &error)) return 1;
    Interpreter interp(&linker);
    DexJniEnv jni(&linker, &interp);
    interp.set_jni_env(&jni);
    interp.set_instruction_limit(2000ull * 1000ull * 1000ull);
    g_linker = &linker;
    g_interp = &interp;
    for (size_t length : {0u, 199u, 4096u}) {
        const std::string value(length, 'a');
        auto* matcher = Matcher("([\\w\\ ]*)\\s*:\\s([^\\n]*)", "Features\t: " + value);
        DexValue result;
        if (matcher && CallVirtual(matcher, "find", "()Z", {}, &result, "long key/value find")) {
            Check(result.i == 1, "long key/value matches at length " + std::to_string(length));
            if (result.i) { Group(matcher, 1, "Features"); Group(matcher, 2, value); }
        }
    }
    const std::string longText(1024, 'a');
    auto* matcher = Matcher("([^\\n]*)end", longText + "end");
    DexValue result;
    if (matcher && CallVirtual(matcher, "matches", "()Z", {}, &result, "backtrack before suffix")) {
        Check(result.i == 1, "greedy class backtracks before suffix");
        if (result.i) Group(matcher, 1, longText);
    }
    Matches("[^\\n]+", "", false);
    Matches("[^\\n]*", "", true);
    Matches("[^\\n]+", longText, true);
    Matches("[^\\n]*end", longText, false);
    Matches("[^x]*", longText + u8"😀𝄞z", true);
    Matches(u8"[😀]+", std::string(u8"😀😀"), true);
    Matches(u8"[^ä]+", u8"Ä", false, 66);
    Matches(u8"[^ä]+", u8"Ö😀", true, 66);
    matcher = Matcher(u8"([^x]*)(😀)", longText + u8"😀😀");
    if (matcher && CallVirtual(matcher, "matches", "()Z", {}, &result, "supplementary backtrack")) {
        Check(result.i == 1, "backtrack keeps supplementary boundaries");
        if (result.i) { Group(matcher, 1, longText + u8"😀"); Group(matcher, 2, u8"😀"); }
    }
    for (int left : {0, 1}) {
        matcher = Matcher("[^x]+", u8"😀");
        if (matcher && CallVirtual(matcher, "region", "(II)Ljava/util/regex/Matcher;",
                                 {DexValue::Int(left), DexValue::Int(left + 1)}, &result, "region") &&
            CallVirtual(matcher, "matches", "()Z", {}, &result, "isolated surrogate region"))
            Check(result.i == 1, "surrogate at a region boundary remains matchable");
    }
    Matches("[^x]*?end", "aaaaend", true);
    Matches("[^x]*+end", "aaaaend", false);
    matcher = Matcher("([ab])+c", "abbc");
    if (matcher && CallVirtual(matcher, "matches", "()Z", {}, &result, "repeated captured group")) {
        Check(result.i == 1, "repeated capturing group retains existing behavior");
        if (result.i) Group(matcher, 1, "b");
    }
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
