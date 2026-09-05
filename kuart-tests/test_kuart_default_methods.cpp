// test_kuart_default_methods.cpp — interface default method resolution per JVMS 5.4.3.3.
// Depth-first search returned the first match instead of the most specific, silently
// running overridden bodies; abstract declarations must not outrank concrete defaults.
#include "kudroid/framework_dex_bytes.h"
#include "kudroid/kuart/DexClassLinker.h"
#include "kudroid/kuart/DexJniEnv.h"
#include "kudroid/kuart/DexObject.h"
#include "kudroid/kuart/Interpreter.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "dex_builder.h"

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    std::printf("%s %s\n", ok ? "  OK  " : "  FAIL", what.c_str());
    if (!ok) ++g_failures;
}

using namespace dexbuild;
using kudroid::kuart::DexClass;
using kudroid::kuart::DexClassLinker;
using kudroid::kuart::DexMethod;
using kudroid::kuart::DexObject;
using kudroid::kuart::DexValue;
using kudroid::kuart::Interpreter;

void Op21s(std::vector<uint16_t>* code, uint8_t op, uint8_t a, int16_t v) {
    code->push_back(static_cast<uint16_t>(op | (a << 8)));
    code->push_back(static_cast<uint16_t>(v));
}
void Op35c(std::vector<uint16_t>* code, uint8_t op, uint16_t idx,
           const std::vector<uint8_t>& regs) {
    code->push_back(static_cast<uint16_t>(op | (regs.size() << 12)));
    code->push_back(idx);
    uint16_t packed = 0;
    for (size_t i = 0; i < regs.size() && i < 4; ++i) {
        packed |= static_cast<uint16_t>((regs[i] & 0xF) << (i * 4));
    }
    code->push_back(packed);
}

constexpr uint8_t kOpConst16 = 0x13;
constexpr uint8_t kOpReturn = 0x0f;
constexpr uint8_t kOpReturnVoid = 0x0e;
constexpr uint8_t kOpMoveResult = 0x0a;
constexpr uint8_t kOpInvokeVirtual = 0x6e;
constexpr uint8_t kOpInvokeInterface = 0x72;

constexpr uint32_t kAccPublic = 0x1;
constexpr uint32_t kAccStatic = 0x8;
constexpr uint32_t kAccPublicStatic = kAccPublic | kAccStatic;
constexpr uint32_t kAccInterface = 0x200;
constexpr uint32_t kAccAbstract = 0x400;
constexpr uint32_t kAccConstructor = 0x10000;

// The class graph under test.
//
//   interface J          { default int f() { return 1; }  int g(); }
//   interface I extends J { default int f() { return 2; } }
//   abstract class B implements I { public abstract int g(); }   // NO f()
//   class D extends B    { public int g() { return 30; } }
//   class C implements J, I { public int g() { return 40; } }    // J listed FIRST
//
// D.f() must reach I.f() -> 2, through B's abstract-free chain.
// C.f() must reach I.f() -> 2, not J.f() -> 1, because I extends J.
//
// The method references the caller's bytecode needs. Held in a struct because a DEX
// method index is only known AFTER the tables are sorted, so the classes are built twice:
// once to learn the indices, once with the real ones patched into the bytecode. Writing a
// literal index would encode whatever the sort happened to produce, and the call would
// silently target a different method when anything else in the file changed.
struct Refs {
    MethodRefSpec j_f{"Lcom/foo/J;", "f", "I", {}};
    MethodRefSpec d_f{"Lcom/foo/D;", "f", "I", {}};
};

std::vector<ClassSpec> BuildClasses(const Refs& refs, uint16_t j_f_idx, uint16_t d_f_idx) {
    // interface J
    MethodSpec j_f;
    j_f.name = "f";
    j_f.return_type = "I";
    j_f.access_flags = kAccPublic;  // NOT abstract: a default method has a body
    Op21s(&j_f.code, kOpConst16, 0, 1);
    j_f.code.push_back(static_cast<uint16_t>(kOpReturn | (0 << 8)));
    j_f.registers_size = 2;  // v0 result, v1 this
    j_f.ins_size = 1;

    MethodSpec j_g;
    j_g.name = "g";
    j_g.return_type = "I";
    j_g.access_flags = kAccPublic | kAccAbstract;

    ClassSpec j;
    j.descriptor = "Lcom/foo/J;";
    j.access_flags = kAccPublic | kAccInterface | kAccAbstract;
    j.virtual_methods = {j_f, j_g};

    // interface I extends J — overrides the default
    MethodSpec i_f;
    i_f.name = "f";
    i_f.return_type = "I";
    i_f.access_flags = kAccPublic;
    Op21s(&i_f.code, kOpConst16, 0, 2);
    i_f.code.push_back(static_cast<uint16_t>(kOpReturn | (0 << 8)));
    i_f.registers_size = 2;
    i_f.ins_size = 1;

    ClassSpec i;
    i.descriptor = "Lcom/foo/I;";
    i.access_flags = kAccPublic | kAccInterface | kAccAbstract;
    // An interface "extends" another by listing it as an interface; its superclass is
    // still Object.
    i.interfaces = {"Lcom/foo/J;"};
    i.virtual_methods = {i_f};

    // abstract class B implements I — declares g() abstract, does NOT declare f()
    MethodSpec b_ctor;
    b_ctor.name = "<init>";
    b_ctor.access_flags = kAccPublic | kAccConstructor;
    b_ctor.code.push_back(kOpReturnVoid);
    b_ctor.registers_size = 1;
    b_ctor.ins_size = 1;

    MethodSpec b_g;
    b_g.name = "g";
    b_g.return_type = "I";
    b_g.access_flags = kAccPublic | kAccAbstract;

    ClassSpec b;
    b.descriptor = "Lcom/foo/B;";
    b.access_flags = kAccPublic | kAccAbstract;
    b.interfaces = {"Lcom/foo/I;"};
    b.direct_methods = {b_ctor};
    b.virtual_methods = {b_g};

    // class D extends B
    MethodSpec d_ctor;
    d_ctor.name = "<init>";
    d_ctor.access_flags = kAccPublic | kAccConstructor;
    d_ctor.code.push_back(kOpReturnVoid);
    d_ctor.registers_size = 1;
    d_ctor.ins_size = 1;

    MethodSpec d_g;
    d_g.name = "g";
    d_g.return_type = "I";
    d_g.access_flags = kAccPublic;
    Op21s(&d_g.code, kOpConst16, 0, 30);
    d_g.code.push_back(static_cast<uint16_t>(kOpReturn | (0 << 8)));
    d_g.registers_size = 2;
    d_g.ins_size = 1;

    ClassSpec d;
    d.descriptor = "Lcom/foo/D;";
    d.superclass = "Lcom/foo/B;";
    d.access_flags = kAccPublic;
    d.direct_methods = {d_ctor};
    d.virtual_methods = {d_g};

    // class C implements J, I — J deliberately listed FIRST, so depth-first order finds
    // the superseded default before the specific one.
    MethodSpec c_ctor;
    c_ctor.name = "<init>";
    c_ctor.access_flags = kAccPublic | kAccConstructor;
    c_ctor.code.push_back(kOpReturnVoid);
    c_ctor.registers_size = 1;
    c_ctor.ins_size = 1;

    MethodSpec c_g;
    c_g.name = "g";
    c_g.return_type = "I";
    c_g.access_flags = kAccPublic;
    Op21s(&c_g.code, kOpConst16, 0, 40);
    c_g.code.push_back(static_cast<uint16_t>(kOpReturn | (0 << 8)));
    c_g.registers_size = 2;
    c_g.ins_size = 1;

    ClassSpec c;
    c.descriptor = "Lcom/foo/C;";
    c.access_flags = kAccPublic;
    c.interfaces = {"Lcom/foo/J;", "Lcom/foo/I;"};
    c.direct_methods = {c_ctor};
    c.virtual_methods = {c_g};

    // class E implements J only — the plain case, to prove the fix did not break it.
    MethodSpec e_ctor;
    e_ctor.name = "<init>";
    e_ctor.access_flags = kAccPublic | kAccConstructor;
    e_ctor.code.push_back(kOpReturnVoid);
    e_ctor.registers_size = 1;
    e_ctor.ins_size = 1;

    MethodSpec e_g;
    e_g.name = "g";
    e_g.return_type = "I";
    e_g.access_flags = kAccPublic;
    Op21s(&e_g.code, kOpConst16, 0, 50);
    e_g.code.push_back(static_cast<uint16_t>(kOpReturn | (0 << 8)));
    e_g.registers_size = 2;
    e_g.ins_size = 1;

    ClassSpec e;
    e.descriptor = "Lcom/foo/E;";
    e.access_flags = kAccPublic;
    e.interfaces = {"Lcom/foo/J;"};
    e.direct_methods = {e_ctor};
    e.virtual_methods = {e_g};

    // class Ov implements I but overrides f() itself — a class method must beat any
    // default, or a class could not override one.
    MethodSpec ov_ctor;
    ov_ctor.name = "<init>";
    ov_ctor.access_flags = kAccPublic | kAccConstructor;
    ov_ctor.code.push_back(kOpReturnVoid);
    ov_ctor.registers_size = 1;
    ov_ctor.ins_size = 1;

    MethodSpec ov_f;
    ov_f.name = "f";
    ov_f.return_type = "I";
    ov_f.access_flags = kAccPublic;
    Op21s(&ov_f.code, kOpConst16, 0, 99);
    ov_f.code.push_back(static_cast<uint16_t>(kOpReturn | (0 << 8)));
    ov_f.registers_size = 2;
    ov_f.ins_size = 1;

    MethodSpec ov_g;
    ov_g.name = "g";
    ov_g.return_type = "I";
    ov_g.access_flags = kAccPublic;
    Op21s(&ov_g.code, kOpConst16, 0, 60);
    ov_g.code.push_back(static_cast<uint16_t>(kOpReturn | (0 << 8)));
    ov_g.registers_size = 2;
    ov_g.ins_size = 1;

    ClassSpec ov;
    ov.descriptor = "Lcom/foo/Ov;";
    ov.access_flags = kAccPublic;
    ov.interfaces = {"Lcom/foo/I;"};
    ov.direct_methods = {ov_ctor};
    ov.virtual_methods = {ov_f, ov_g};

    // A caller with real bytecode: invoke-interface J.f() on whatever it is handed, and
    // invoke-virtual on the class type. Both routes must reach the same body.
    MethodSpec call_iface;
    call_iface.name = "callThroughInterface";
    call_iface.return_type = "I";
    call_iface.params = {"Lcom/foo/J;"};
    call_iface.access_flags = kAccPublicStatic;
    Op35c(&call_iface.code, kOpInvokeInterface, j_f_idx, {0});
    call_iface.code.push_back(static_cast<uint16_t>(kOpMoveResult | (0 << 8)));
    call_iface.code.push_back(static_cast<uint16_t>(kOpReturn | (0 << 8)));
    call_iface.registers_size = 1;
    call_iface.ins_size = 1;
    call_iface.outs_size = 1;

    MethodSpec call_virtual;
    call_virtual.name = "callThroughClass";
    call_virtual.return_type = "I";
    call_virtual.params = {"Lcom/foo/D;"};
    call_virtual.access_flags = kAccPublicStatic;
    Op35c(&call_virtual.code, kOpInvokeVirtual, d_f_idx, {0});
    call_virtual.code.push_back(static_cast<uint16_t>(kOpMoveResult | (0 << 8)));
    call_virtual.code.push_back(static_cast<uint16_t>(kOpReturn | (0 << 8)));
    call_virtual.registers_size = 1;
    call_virtual.ins_size = 1;
    call_virtual.outs_size = 1;

    ClassSpec caller;
    caller.descriptor = "Lcom/foo/Caller;";
    caller.access_flags = kAccPublic;
    caller.direct_methods = {call_iface, call_virtual};
    // The bytecode above references J.f() and D.f() without declaring them here.
    caller.extra_method_refs = {refs.j_f, refs.d_f};

    return {j, i, b, d, c, e, ov, caller};
}

// Call a static int(Object) helper and return its result.
int CallHelper(Interpreter* interp, DexClass* caller, const char* helper,
               const char* signature, DexObject* arg, bool* threw) {
    DexMethod* m = caller->FindDirectMethod(helper, signature);
    if (m == nullptr) {
        *threw = true;
        return -1;
    }
    DexValue a = DexValue::Ref(arg);
    interp->ClearPendingException();
    const DexValue r = interp->Execute(m, &a, 1);
    *threw = interp->HasPendingException();
    interp->ClearPendingException();
    return r.i;
}

}  // namespace

int main() {
    std::printf("=== KuART interface default methods ===\n");

    // Two passes: the first learns the method indices the sorted tables produce, the
    // second builds the real DEX with those indices patched into the caller's bytecode.
    const Refs refs;
    DexBuilder probe;
    probe.Build(BuildClasses(refs, 0, 0));
    const uint16_t j_f_idx = static_cast<uint16_t>(probe.MethodRefIndexOf(refs.j_f));
    const uint16_t d_f_idx = static_cast<uint16_t>(probe.MethodRefIndexOf(refs.d_f));

    DexBuilder builder;
    const std::vector<uint8_t> dex = builder.Build(BuildClasses(refs, j_f_idx, d_f_idx));

    DexClassLinker linker;
    std::string error;
    if (!linker.AddDexFile(g_framework_dex_bytes, g_framework_dex_size,
                           "framework.dex", &error)) {
        std::printf("  FAIL framework.dex: %s\n=== FAILED ===\n", error.c_str());
        return 1;
    }
    if (!linker.AddDexFile(dex.data(), dex.size(), "test.dex", &error)) {
        std::printf("  FAIL test.dex: %s\n=== FAILED ===\n", error.c_str());
        return 1;
    }

    Interpreter interp(&linker);
    kudroid::kuart::DexJniEnv jni(&linker, &interp);
    interp.set_jni_env(&jni);

    DexClass* j = linker.FindClass("Lcom/foo/J;");
    DexClass* i = linker.FindClass("Lcom/foo/I;");
    DexClass* d = linker.FindClass("Lcom/foo/D;");
    DexClass* c = linker.FindClass("Lcom/foo/C;");
    DexClass* e = linker.FindClass("Lcom/foo/E;");
    DexClass* ov = linker.FindClass("Lcom/foo/Ov;");
    DexClass* caller = linker.FindClass("Lcom/foo/Caller;");
    if (!j || !i || !d || !c || !e || !ov || !caller) {
        std::printf("  FAIL missing test classes\n=== FAILED ===\n");
        return 1;
    }

    // ── an abstract declaration in the class chain must not hide a default ──
    //
    // D extends B, B declares g() abstract and does not mention f() at all, and f()'s only
    // implementation is I's default. Resolution used to stop inside the class chain.
    //
    // javac cannot produce this exact shape — it demands that a concrete D override any
    // abstract method it inherits — so this covers DEX from producers that are not javac.
    {
        std::printf("[default] a class whose chain has no f() reaches the interface default\n");
        DexMethod* resolved = d->FindVirtualMethod("f", "()I");
        Check(resolved != nullptr, "D.f() resolves to something");
        Check(resolved != nullptr && !resolved->IsAbstract(),
              "and it is a concrete method, not an abstract declaration");
        Check(resolved != nullptr && resolved->declaring_class == i,
              "specifically I.f(), the maximally specific default");

        DexObject* obj = linker.AllocObject(d);
        Check(obj != nullptr, "allocated a D");

        bool threw = false;
        const int through_class = CallHelper(&interp, caller, "callThroughClass",
                                             "(Lcom/foo/D;)I", obj, &threw);
        Check(!threw, "invoke-virtual D.f() threw nothing rather than AbstractMethodError");
        Check(through_class == 2, std::string("and returned I.f()'s 2, got ") +
                                     std::to_string(through_class));

        threw = false;
        const int through_iface = CallHelper(&interp, caller, "callThroughInterface",
                                             "(Lcom/foo/J;)I", obj, &threw);
        Check(!threw, "invoke-interface J.f() on a D threw nothing");
        // Both dispatch routes must agree, or the same object behaves differently
        // depending on the static type at the call site.
        Check(through_iface == through_class,
              "invoke-interface and invoke-virtual reach the same body");
    }

    // ── the MOST SPECIFIC default wins, not the first one listed ──
    {
        std::printf("[default] I extends J, so I.f() wins however C lists them\n");
        DexMethod* resolved = c->FindVirtualMethod("f", "()I");
        Check(resolved != nullptr, "C.f() resolves");
        Check(resolved != nullptr && resolved->declaring_class == i,
              "to I.f(), not to J.f() which I overrides — C lists J first, so depth-first "
              "order would have picked J");

        DexObject* obj = linker.AllocObject(c);
        bool threw = false;
        const int got = CallHelper(&interp, caller, "callThroughInterface",
                                   "(Lcom/foo/J;)I", obj, &threw);
        Check(!threw, "the call threw nothing");
        Check(got == 2, std::string("and ran I.f() -> 2, not J.f() -> 1; got ") +
                            std::to_string(got));
    }

    // ── a single interface still works ──
    {
        std::printf("[default] the ordinary single-interface case is unaffected\n");
        DexMethod* resolved = e->FindVirtualMethod("f", "()I");
        Check(resolved != nullptr && resolved->declaring_class == j,
              "E implements only J, so J.f() is the answer");

        DexObject* obj = linker.AllocObject(e);
        bool threw = false;
        const int got = CallHelper(&interp, caller, "callThroughInterface",
                                   "(Lcom/foo/J;)I", obj, &threw);
        Check(!threw && got == 1,
              std::string("J.f() -> 1, got ") + std::to_string(got));
    }

    // ── a class method beats a default ──
    //
    // The inverse error: fixing the above by consulting interfaces FIRST would break
    // overriding entirely, which is the whole point of a default method being a default.
    {
        std::printf("[default] a class's own override beats the interface default\n");
        DexMethod* resolved = ov->FindVirtualMethod("f", "()I");
        Check(resolved != nullptr && resolved->declaring_class == ov,
              "Ov declares f() itself, so Ov.f() wins over I.f()");

        DexObject* obj = linker.AllocObject(ov);
        bool threw = false;
        const int got = CallHelper(&interp, caller, "callThroughInterface",
                                   "(Lcom/foo/J;)I", obj, &threw);
        Check(!threw && got == 99,
              std::string("the override ran -> 99, got ") + std::to_string(got));
    }

    // ── a genuinely abstract method still reports itself ──
    //
    // The fix must not turn every AbstractMethodError into a silent success: g() has no
    // implementation anywhere on B, and a call must still say so rather than returning 0.
    {
        std::printf("[default] a method with no implementation anywhere still errors\n");
        DexClass* b = linker.FindClass("Lcom/foo/B;");
        Check(b != nullptr, "B is present");
        DexMethod* resolved = b != nullptr ? b->FindVirtualMethod("g", "()I") : nullptr;
        Check(resolved != nullptr, "B.g() resolves to the abstract declaration");
        Check(resolved != nullptr && resolved->IsAbstract(),
              "and it is reported as abstract, so the caller can raise AbstractMethodError");
        Check(resolved != nullptr && resolved->declaring_class == b,
              "naming B, the most derived declaration — the right name for the error");
    }

    // ── an overriding class method is still found when the interface also declares it ──
    {
        std::printf("[default] an abstract interface method does not shadow the class body\n");
        DexMethod* resolved = d->FindVirtualMethod("g", "()I");
        Check(resolved != nullptr && !resolved->IsAbstract() &&
                  resolved->declaring_class == d,
              "D.g() is D's concrete body, not J's or B's abstract declaration");
    }

    std::printf("=== %s (%d checks, %d error) ===\n",
                g_failures == 0 ? "PASSED" : "FAILED", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
