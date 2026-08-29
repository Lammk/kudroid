// Probe: leaving the interpreter without unwinding the C++ stack, and receivers
// whose class pointer came from native code.
//
// Both failures were found in the same Minecraft launch and both hid the real
// problem rather than being the problem.
//
// 1. The JNI_OnLoad shield in kudroid_bridge.cpp siglongjmps out of a library that
//    faults mid-callback. siglongjmp does not run destructors, so every
//    Interpreter::Execute frame the library had entered kept its scope-guard entry:
//    the call-stack vector held pointers into reclaimed C++ stack, the depth counter
//    never came down, and the VM lock was never released. The next exception on that
//    thread rendered a trace from the dead entries and took SIGSEGV inside
//    BuildStackTrace — a crash in the code whose entire job was to explain the
//    failure. MainActivity.onCreate died this way and its exception was never printed.
//
// 2. DexObject::clazz and DexClass::descriptor both sit at offset 0, so a jclass
//    passed where a jobject was expected reads back the descriptor STRING pointer and
//    it gets used as a class: SIGSEGV in FindVirtualMethod at 0x2f657074666172eb, the
//    ASCII bytes "raftpe/". A stale handle gives the other shape — a clazz like 0x10,
//    non-null so an ordinary null check waves it through, faulting at a small offset.
#include "kudroid/kuart/DexClassLinker.h"
#include "kudroid/kuart/DexJniEnv.h"
#include "kudroid/kuart/DexObject.h"
#include "kudroid/kuart/Interpreter.h"
#include "kudroid/kuart/VmLock.h"

#include <csetjmp>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "dex_builder.h"

namespace {

using dexbuild::ClassSpec;
using dexbuild::DexBuilder;
using dexbuild::FieldSpec;
using dexbuild::MethodSpec;
using kudroid::kuart::DexValue;

int g_failures = 0;

void Check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "  OK  " : "  FAIL", what.c_str());
    if (!ok) ++g_failures;
}

constexpr uint8_t kOpReturnVoid = 0x0e;
constexpr uint8_t kOpReturn = 0x0f;
constexpr uint8_t kOpMoveResult = 0x0a;
constexpr uint8_t kOpConst4 = 0x12;
constexpr uint8_t kOpIget = 0x52;
constexpr uint8_t kOpInvokeVirtual = 0x6e;
constexpr uint8_t kOpInvokeStatic = 0x71;

constexpr uint32_t kAccPublicStaticNative = 0x1 | 0x8 | 0x100;

uint16_t Op11x(uint8_t op, uint8_t a) { return static_cast<uint16_t>(op | (a << 8)); }
uint16_t Op11n(uint8_t op, uint8_t a, int8_t b) {
    return static_cast<uint16_t>(op | (a << 8) | ((b & 0xF) << 12));
}
void Op22c(std::vector<uint16_t>* code, uint8_t op, uint8_t a, uint8_t b, uint16_t idx) {
    code->push_back(static_cast<uint16_t>(op | (a << 8) | (b << 12)));
    code->push_back(idx);
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

struct Specs {
    FieldSpec value{"value", "I", 0x1};

    MethodSpec ctor;
    MethodSpec get_value;    // virtual: return this.value
    MethodSpec bail;         // static native: longjmps out of the interpreter
    MethodSpec call_bail;    // static: invoke bail  (two frames deep)
    MethodSpec outer_bail;   // static: invoke callBail
    MethodSpec call_virtual; // static: invoke-virtual getValue on the argument
    MethodSpec plain;        // static: return 7, nothing unusual

    Specs() {
        ctor.name = "<init>";
        ctor.access_flags = 0x10001;

        get_value.name = "getValue";
        get_value.return_type = "I";

        bail.name = "bail";
        bail.access_flags = kAccPublicStaticNative;

        call_bail.name = "callBail";
        call_bail.access_flags = 0x9;

        outer_bail.name = "outerBail";
        outer_bail.access_flags = 0x9;

        call_virtual.name = "callVirtual";
        call_virtual.return_type = "I";
        call_virtual.params = {"LT;"};
        call_virtual.access_flags = 0x9;

        plain.name = "plain";
        plain.return_type = "I";
        plain.access_flags = 0x9;
    }
};

std::vector<ClassSpec> BuildClasses(const Specs& s) {
    ClassSpec object;
    object.descriptor = "Ljava/lang/Object;";
    object.superclass = "";
    {
        MethodSpec ctor;
        ctor.name = "<init>";
        ctor.access_flags = 0x10001;
        ctor.code = {Op11x(kOpReturnVoid, 0)};
        ctor.registers_size = 1;
        ctor.ins_size = 1;
        object.direct_methods.push_back(ctor);
    }

    ClassSpec string;
    string.descriptor = "Ljava/lang/String;";
    string.superclass = "Ljava/lang/Object;";

    ClassSpec class_class;
    class_class.descriptor = "Ljava/lang/Class;";
    class_class.superclass = "Ljava/lang/Object;";

    // The interpreter needs these to exist to report the failures under test.
    ClassSpec throwable;
    throwable.descriptor = "Ljava/lang/Throwable;";
    throwable.superclass = "Ljava/lang/Object;";
    throwable.instance_fields = {FieldSpec{"message", "Ljava/lang/String;", 0x1}};

    ClassSpec illegal_state;
    illegal_state.descriptor = "Ljava/lang/IllegalStateException;";
    illegal_state.superclass = "Ljava/lang/Throwable;";

    ClassSpec arith;
    arith.descriptor = "Ljava/lang/ArithmeticException;";
    arith.superclass = "Ljava/lang/Throwable;";

    ClassSpec t;
    t.descriptor = "LT;";
    t.superclass = "Ljava/lang/Object;";
    t.instance_fields = {s.value};
    t.direct_methods = {s.ctor,         s.bail,  s.call_bail,
                        s.outer_bail,   s.plain, s.call_virtual};
    t.virtual_methods = {s.get_value};

    return {object, string, class_class, throwable, illegal_state, arith, t};
}

// Where the native method jumps back to. A plain sigsetjmp/siglongjmp pair models
// the shield exactly for the purpose of this test: the distinguishing property is
// that the C++ frames in between are abandoned without their destructors running,
// which is true of longjmp whether or not a signal handler is involved.
sigjmp_buf g_bail_jmp;
volatile bool g_bail_armed = false;

void JNICALL Bail(JNIEnv*, jclass) {
    if (g_bail_armed) {
        g_bail_armed = false;
        siglongjmp(g_bail_jmp, 1);
    }
}

// Overwrite the stack region the abandoned frames occupied.
//
// Immediately after a siglongjmp that memory still holds the old frames, so reading
// a dead DexFrame appears to work and the bug hides. In the real failure a great deal
// runs between the jump and the next exception — the shield logs, the loader moves on
// to the next library, the app carries on into onCreate — and by then the DexFrame
// objects are long gone. Reproduce that rather than relying on luck.
//
// volatile and a returned value so the writes cannot be optimised away.
__attribute__((noinline)) uint64_t ClobberStack() {
    volatile uint64_t scratch[4096];
    for (size_t i = 0; i < 4096; ++i) scratch[i] = 0xDEADBEEFCAFEBABEull;
    uint64_t sum = 0;
    for (size_t i = 0; i < 4096; ++i) sum += scratch[i];
    return sum;
}

}  // namespace

int main() {
    std::printf("=== KuART thread state after a non-unwinding exit ===\n");

    Specs s;
    s.ctor.code = {Op11x(kOpReturnVoid, 0)};
    s.ctor.registers_size = 1;
    s.ctor.ins_size = 1;

    Specs probe;
    DexBuilder index_builder;
    index_builder.Build(BuildClasses(probe));
    const uint16_t kBailIdx =
        static_cast<uint16_t>(index_builder.MethodIndexOf("LT;", probe.bail));
    const uint16_t kCallBailIdx =
        static_cast<uint16_t>(index_builder.MethodIndexOf("LT;", probe.call_bail));
    const uint16_t kGetValueIdx =
        static_cast<uint16_t>(index_builder.MethodIndexOf("LT;", probe.get_value));
    const uint16_t kValueIdx =
        static_cast<uint16_t>(index_builder.FieldIndexOf("LT;", probe.value));

    // int getValue() { return this.value; }
    {
        std::vector<uint16_t> c;
        Op22c(&c, kOpIget, 0, 1, kValueIdx);
        c.push_back(Op11x(kOpReturn, 0));
        s.get_value.code = c;
        s.get_value.registers_size = 2;
        s.get_value.ins_size = 1;
    }
    // void callBail() { bail(); }
    {
        std::vector<uint16_t> c;
        Op35c(&c, kOpInvokeStatic, kBailIdx, {});
        c.push_back(Op11x(kOpReturnVoid, 0));
        s.call_bail.code = c;
        s.call_bail.registers_size = 1;
        s.call_bail.outs_size = 1;
    }
    // void outerBail() { callBail(); }  — so more than one frame is abandoned.
    {
        std::vector<uint16_t> c;
        Op35c(&c, kOpInvokeStatic, kCallBailIdx, {});
        c.push_back(Op11x(kOpReturnVoid, 0));
        s.outer_bail.code = c;
        s.outer_bail.registers_size = 1;
        s.outer_bail.outs_size = 1;
    }
    // int callVirtual(T t) { return t.getValue(); }
    {
        std::vector<uint16_t> c;
        Op35c(&c, kOpInvokeVirtual, kGetValueIdx, {0});
        c.push_back(Op11x(kOpMoveResult, 0));
        c.push_back(Op11x(kOpReturn, 0));
        s.call_virtual.code = c;
        s.call_virtual.registers_size = 1;
        s.call_virtual.ins_size = 1;
        s.call_virtual.outs_size = 1;
    }
    // int plain() { return 7; }
    {
        std::vector<uint16_t> c;
        c.push_back(Op11n(kOpConst4, 0, 7));
        c.push_back(Op11x(kOpReturn, 0));
        s.plain.code = c;
        s.plain.registers_size = 1;
    }

    DexBuilder builder;
    const std::vector<uint8_t> dex = builder.Build(BuildClasses(s));

    kudroid::kuart::DexClassLinker linker;
    std::string error;
    if (!linker.AddDexFile(dex.data(), dex.size(), "threadstate.dex", &error)) {
        std::printf("  FAIL AddDexFile: %s\n=== FAILED ===\n", error.c_str());
        return 1;
    }

    kudroid::kuart::Interpreter interp(&linker);
    kudroid::kuart::DexJniEnv jni(&linker, &interp);
    interp.set_jni_env(&jni);

    kudroid::kuart::DexClass* t = linker.FindClass("LT;");
    if (t == nullptr) {
        std::printf("  FAIL FindClass(LT;)\n=== FAILED ===\n");
        return 1;
    }

    {
        char name[] = "bail";
        char sig[] = "()V";
        const JNINativeMethod natives[] = {{name, sig, reinterpret_cast<void*>(&Bail)}};
        Check(jni.RegisterNatives(t, natives, 1) == JNI_OK, "RegisterNatives(bail)");
    }

    kudroid::kuart::DexMethod* outer = t->FindDirectMethod("outerBail", "()V");
    kudroid::kuart::DexMethod* plain = t->FindDirectMethod("plain", "()I");
    Check(outer != nullptr && plain != nullptr, "test methods resolved");
    if (outer == nullptr || plain == nullptr) {
        std::printf("=== FAILED ===\n");
        return 1;
    }

    Check(kudroid::kuart::VmLockDepth() == 0, "VM lock starts unheld");

    // ── the leak, observed ──
    //
    // Jump out from two frames down and look at what the interpreter believes
    // afterwards. This is the state the old code stayed in.
    kudroid::kuart::Interpreter::ThreadState saved =
        kudroid::kuart::Interpreter::CaptureThreadState();
    Check(saved.depth == 0 && saved.frames == 0 && saved.vm_lock_depth == 0,
          "captured state at rest is all zero");

    g_bail_armed = true;
    if (sigsetjmp(g_bail_jmp, 1) == 0) {
        interp.Execute(outer, nullptr, 0);
        Check(false, "the native method was supposed to jump out");
    } else {
        // Nothing here ran a destructor. The frames are gone but the interpreter has
        // not been told.
        const kudroid::kuart::Interpreter::ThreadState leaked =
            kudroid::kuart::Interpreter::CaptureThreadState();
        Check(leaked.frames == 2, "both abandoned frames are still recorded");
        Check(leaked.depth == 2, "the depth counter is still raised");
        Check(leaked.vm_lock_depth == 1, "the VM lock is still held");

        // The trace is built from entries whose DexFrame storage has been reclaimed
        // AND overwritten, which is the state the real failure was in by the time the
        // next exception arrived. It must not fault: the method name comes from the
        // linker heap, which outlives every frame. Reading a garbage DexMethod out of
        // a dead frame is what used to crash here.
        const uint64_t clobbered = ClobberStack();
        Check(clobbered != 0, "stack under the abandoned frames overwritten");
        const std::string stale = interp.BuildStackTrace();
        Check(stale.find("T.callBail") != std::string::npos,
              "a trace over abandoned frames still names them, without faulting");

        kudroid::kuart::Interpreter::RestoreThreadState(saved);
    }

    // ── after the repair ──
    {
        const kudroid::kuart::Interpreter::ThreadState now =
            kudroid::kuart::Interpreter::CaptureThreadState();
        Check(now.frames == 0, "abandoned frames dropped");
        Check(now.depth == 0, "depth counter back to zero");
        Check(now.vm_lock_depth == 0, "VM lock released");
        Check(kudroid::kuart::VmLockDepth() == 0,
              "VM lock genuinely unheld, so other Java threads can run");
        Check(interp.BuildStackTrace().empty(), "no stale frames left to report");
    }

    // The interpreter is usable again, not merely tidy.
    {
        interp.ClearPendingException();
        const DexValue r = interp.Execute(plain, nullptr, 0);
        Check(!interp.HasPendingException() && r.i == 7,
              "a normal call works after the repair");
    }

    // ── the depth counter, which is the part that eventually killed the process ──
    //
    // kMaxCallDepth is 512, so 600 unrepaired jumps would leave every later call
    // throwing StackOverflowError with no plausible cause in sight. Each iteration
    // leaks two frames.
    {
        bool all_ok = true;
        for (int i = 0; i < 600 && all_ok; ++i) {
            const kudroid::kuart::Interpreter::ThreadState before =
                kudroid::kuart::Interpreter::CaptureThreadState();
            g_bail_armed = true;
            if (sigsetjmp(g_bail_jmp, 1) == 0) {
                interp.Execute(outer, nullptr, 0);
                all_ok = false;  // should not return normally
            } else {
                kudroid::kuart::Interpreter::RestoreThreadState(before);
            }
        }
        Check(all_ok, "600 abandoned calls, each one jumped out as expected");

        interp.ClearPendingException();
        const DexValue r = interp.Execute(plain, nullptr, 0);
        Check(!interp.HasPendingException() && r.i == 7,
              "no StackOverflowError after 600 abandoned calls (1200 leaked frames)");
        Check(kudroid::kuart::VmLockDepth() == 0, "VM lock still unheld after 600");
    }

    // ── receivers whose class pointer cannot be trusted ──

    kudroid::kuart::DexMethod* call_virtual = t->FindDirectMethod("callVirtual", "(LT;)I");
    Check(call_virtual != nullptr, "callVirtual resolved");

    // The ordinary case, to prove the checks are not rejecting everything.
    kudroid::kuart::DexObject* good = linker.AllocObject(t);
    Check(good != nullptr, "AllocObject");
    if (good != nullptr && call_virtual != nullptr) {
        const kudroid::kuart::DexField* f = t->FindInstanceField("value", "I");
        Check(f != nullptr, "found T.value");
        if (f != nullptr) good->SetField<int32_t>(f->offset_or_slot, 5);

        Check(linker.ClassOfObject(good) == t, "ClassOfObject on a real object");

        interp.ClearPendingException();
        DexValue args[1] = {DexValue::Ref(good)};
        const DexValue r = interp.Execute(call_virtual, args, 1);
        Check(!interp.HasPendingException() && r.i == 5,
              "invoke-virtual on a valid receiver still dispatches");
    }

    // A DexClass handed in as a DexObject. Reading obj->clazz would read
    // DexClass::descriptor — the "raftpe/" crash.
    {
        auto* class_as_object = reinterpret_cast<kudroid::kuart::DexObject*>(t);
        Check(linker.ClassOfObject(class_as_object) == nullptr,
              "ClassOfObject refuses a jclass passed as a jobject");

        // The value it would have produced is the descriptor string, and it must not
        // be mistaken for a class.
        Check(!linker.IsRegisteredClass(
                  reinterpret_cast<const kudroid::kuart::DexClass*>(t->descriptor)),
              "the descriptor pointer is not a registered class");
    }

    // A stale handle: clazz non-null but not a class. libPlayFabMultiplayer produced
    // exactly this, faulting at offset 0x98 after passing the null check.
    if (call_virtual != nullptr) {
        kudroid::kuart::DexObject* bad = linker.AllocObject(t);
        Check(bad != nullptr, "AllocObject for the corrupted case");
        if (bad != nullptr) {
            bad->clazz = reinterpret_cast<kudroid::kuart::DexClass*>(0x10);
            Check(linker.ClassOfObject(bad) == nullptr,
                  "ClassOfObject refuses a non-null clazz that is not a class");

            interp.ClearPendingException();
            DexValue args[1] = {DexValue::Ref(bad)};
            interp.Execute(call_virtual, args, 1);
            Check(interp.HasPendingException(),
                  "invoke-virtual on a corrupted receiver throws instead of faulting");
            const std::string msg = interp.last_error();
            Check(msg.find("invalid class pointer") != std::string::npos,
                  "the exception says what was wrong: " + msg);
            interp.ClearPendingException();
        }
    }

    // JNI virtual dispatch takes the same route and must survive the same handles.
    {
        kudroid::kuart::DexMethod* getter = t->FindVirtualMethod("getValue", "()I");
        Check(getter != nullptr, "found getValue for the JNI path");
        if (getter != nullptr) {
            // A jclass as the receiver: CallJavaA used to reach FindVirtualMethod
            // through it. Returning without dispatching is enough; not crashing is
            // the point.
            jni.CallJavaA(reinterpret_cast<kudroid::kuart::DexObject*>(t), getter, nullptr,
                          /*virtual_dispatch=*/true);
            Check(true, "CallJavaA with a jclass receiver does not fault");

            if (good != nullptr) {
                const DexValue v =
                    jni.CallJavaA(good, getter, nullptr, /*virtual_dispatch=*/true);
                Check(v.i == 5, "CallJavaA still dispatches on a valid receiver");
            }
        }
    }

    std::printf("=== %s (%d error) ===\n", g_failures == 0 ? "PASSED" : "FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}
