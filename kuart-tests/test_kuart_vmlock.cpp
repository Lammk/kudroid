// Probe: a native method that blocks must not hold the VM lock.
//
// Minecraft stopped — did not crash — on this. MainActivity.onCreate called
// nativeWaitCrashManagementSetupComplete, whose disassembly is a mutex lock, a flag test
// and a condition_variable::wait loop: it waits for another thread to set a flag and
// notify. KuART held the VM lock across the native call, so the thread that would set the
// flag could not run any bytecode, and the wait never ended.
//
// The log ended on the line that RESOLVED that symbol, with no error and no crash, which
// reads as if the call never happened rather than as if it never returned.
//
// Android does not have this problem because a thread in the kNative state holds no
// runtime lock. That is the property pinned here:
//
//   1. A blocking native method releases the VM lock, so another Java thread can run.
//   2. Native code can call BACK into Java while in that state, and the callback is still
//      serialised against other Java threads.
//   3. VmLockDepth() reports what is actually held, since (2) is decided from it.
//
// Every wait in this file is bounded. A regression must fail the test, not hang CI.
#include "kudroid/kuart/DexClassLinker.h"
#include "kudroid/kuart/DexJniEnv.h"
#include "kudroid/kuart/DexObject.h"
#include "kudroid/kuart/Interpreter.h"
#include "kudroid/kuart/VmLock.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "dex_builder.h"

namespace {

using dexbuild::ClassSpec;
using dexbuild::DexBuilder;
using dexbuild::MethodSpec;
using kudroid::kuart::DexValue;

int g_failures = 0;

void Check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "  OK  " : "  FAIL", what.c_str());
    if (!ok) ++g_failures;
}

// Long enough that a working implementation never reaches it, short enough that a
// regression reports rather than hangs.
constexpr auto kTimeout = std::chrono::seconds(5);

constexpr uint8_t kOpReturnVoid = 0x0e;
constexpr uint8_t kOpReturn = 0x0f;
constexpr uint8_t kOpConst4 = 0x12;
constexpr uint8_t kOpMoveResult = 0x0a;
constexpr uint8_t kOpInvokeStatic = 0x71;
constexpr uint8_t kOpSget = 0x60;
constexpr uint8_t kOpSput = 0x67;
constexpr uint8_t kOpAddIntLit8 = 0xd8;

constexpr uint32_t kAccPublicStaticNative = 0x1 | 0x8 | 0x100;

uint16_t Op11x(uint8_t op, uint8_t a) { return static_cast<uint16_t>(op | (a << 8)); }
uint16_t Op11n(uint8_t op, uint8_t a, int8_t b) {
    return static_cast<uint16_t>(op | (a << 8) | ((b & 0xF) << 12));
}
void Op21c(std::vector<uint16_t>* code, uint8_t op, uint8_t a, uint16_t idx) {
    code->push_back(static_cast<uint16_t>(op | (a << 8)));
    code->push_back(idx);
}
void Op22b(std::vector<uint16_t>* code, uint8_t op, uint8_t a, uint8_t b, int8_t c) {
    code->push_back(static_cast<uint16_t>(op | (a << 8)));
    code->push_back(static_cast<uint16_t>((b & 0xFF) | ((c & 0xFF) << 8)));
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

// ── the shape of nativeWaitCrashManagementSetupComplete ─────────────────────
//
// Take a mutex, test a flag, wait on a condition variable until it is set. The flag is set
// by other Java code, so the waiting thread must not be holding anything that code needs.
std::mutex g_setup_mutex;
std::condition_variable g_setup_cv;
bool g_setup_done = false;

// Observations made from inside the native call, checked after it returns. Reporting from
// the native method itself would be reporting from a thread that may be wedged.
std::atomic<int> g_depth_inside_native{-1};
std::atomic<bool> g_native_entered{false};
std::atomic<bool> g_native_timed_out{false};

void JNICALL WaitForSetup(JNIEnv*, jclass) {
    // The VM lock must already be released by the time native code runs: this is the
    // reading that says whether a blocking native method can make progress at all.
    g_depth_inside_native.store(kudroid::kuart::VmLockDepth());
    g_native_entered.store(true);

    std::unique_lock<std::mutex> lock(g_setup_mutex);
    if (!g_setup_cv.wait_for(lock, kTimeout, [] { return g_setup_done; })) {
        g_native_timed_out.store(true);
    }
}

// The other half: Java code that sets the flag. On device this is what could not run.
void JNICALL SignalSetup(JNIEnv*, jclass) {
    {
        std::lock_guard<std::mutex> lock(g_setup_mutex);
        g_setup_done = true;
    }
    g_setup_cv.notify_all();
}

// ── native code calling back into Java ──────────────────────────────────────
//
// A native method routinely calls back — a JNI callback, or a predicate evaluated while
// waiting. Such a call arrives with the VM lock released, so it must take the lock for
// itself; if it does not, it interprets bytecode unsynchronised against every other Java
// thread.
kudroid::kuart::Interpreter* g_interp = nullptr;
kudroid::kuart::DexMethod* g_plain_method = nullptr;
std::atomic<int> g_callback_result{-1};
std::atomic<int> g_depth_during_callback{-1};

void JNICALL CallBackIntoJava(JNIEnv*, jclass) {
    if (g_interp == nullptr || g_plain_method == nullptr) return;
    const DexValue r = g_interp->Execute(g_plain_method, nullptr, 0);
    g_callback_result.store(r.i);
    // Inside the callback the lock is held again; by the time Execute returns it is back
    // to released. Sampling after the call shows the guard was scoped to the callback.
    g_depth_during_callback.store(kudroid::kuart::VmLockDepth());
}

// A callback that MUTATES shared Java state, so losing the lock is observable.
//
// The check above proves a callback gets the right answer, but a read-only callback would
// also pass with no lock at all — and keying the guard on the interpreter's call depth
// rather than on whether the lock is held does exactly that: the callback arrives at
// depth > 0, takes no guard, and runs unsynchronised. `counter = counter + 1` is a
// non-atomic read-modify-write in bytecode, so concurrent unlocked callbacks lose
// increments.
kudroid::kuart::DexMethod* g_bump_method = nullptr;

void JNICALL CallBackAndBump(JNIEnv*, jclass) {
    if (g_interp == nullptr || g_bump_method == nullptr) return;
    g_interp->Execute(g_bump_method, nullptr, 0);
}

struct Specs {
    dexbuild::FieldSpec counter{"counter", "I", 0x9};  // public static

    MethodSpec ctor;
    MethodSpec wait_for_setup;   // static native: blocks on the condition variable
    MethodSpec signal_setup;     // static native: sets the flag
    MethodSpec call_back;        // static native: re-enters the interpreter
    MethodSpec call_bump;        // static native: re-enters and mutates static state
    MethodSpec java_wait;        // static: calls waitForSetup
    MethodSpec java_signal;      // static: calls signalSetup
    MethodSpec java_callback;    // static: calls callBack
    MethodSpec java_bump_outer;  // static: calls callBump  (native -> Java -> field)
    MethodSpec bump;             // static: counter = counter + 1
    MethodSpec plain;            // static: return 7

    Specs() {
        ctor.name = "<init>";
        ctor.access_flags = 0x10001;

        wait_for_setup.name = "waitForSetup";
        wait_for_setup.access_flags = kAccPublicStaticNative;

        signal_setup.name = "signalSetup";
        signal_setup.access_flags = kAccPublicStaticNative;

        call_back.name = "callBack";
        call_back.access_flags = kAccPublicStaticNative;

        call_bump.name = "callBump";
        call_bump.access_flags = kAccPublicStaticNative;

        java_wait.name = "javaWait";
        java_wait.access_flags = 0x9;

        java_signal.name = "javaSignal";
        java_signal.access_flags = 0x9;

        java_callback.name = "javaCallback";
        java_callback.access_flags = 0x9;

        java_bump_outer.name = "javaBumpOuter";
        java_bump_outer.access_flags = 0x9;

        bump.name = "bump";
        bump.access_flags = 0x9;

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

    ClassSpec throwable;
    throwable.descriptor = "Ljava/lang/Throwable;";
    throwable.instance_fields = {dexbuild::FieldSpec{"message", "Ljava/lang/String;", 0x1}};

    ClassSpec unsatisfied;
    unsatisfied.descriptor = "Ljava/lang/UnsatisfiedLinkError;";
    unsatisfied.superclass = "Ljava/lang/Throwable;";

    ClassSpec npe;
    npe.descriptor = "Ljava/lang/NullPointerException;";
    npe.superclass = "Ljava/lang/Throwable;";

    ClassSpec t;
    t.descriptor = "LT;";
    t.static_fields = {s.counter};
    t.direct_methods = {s.ctor,          s.wait_for_setup, s.signal_setup,
                        s.call_back,     s.call_bump,      s.java_wait,
                        s.java_signal,   s.java_callback,  s.java_bump_outer,
                        s.bump,          s.plain};

    return {object, string, throwable, unsatisfied, npe, t};
}

}  // namespace

int main() {
    std::printf("=== KuART native calls and the VM lock ===\n");

    Specs probe;
    DexBuilder index_builder;
    index_builder.Build(BuildClasses(probe));
    const uint16_t kWaitIdx =
        static_cast<uint16_t>(index_builder.MethodIndexOf("LT;", probe.wait_for_setup));
    const uint16_t kSignalIdx =
        static_cast<uint16_t>(index_builder.MethodIndexOf("LT;", probe.signal_setup));
    const uint16_t kCallBackIdx =
        static_cast<uint16_t>(index_builder.MethodIndexOf("LT;", probe.call_back));
    const uint16_t kCallBumpIdx =
        static_cast<uint16_t>(index_builder.MethodIndexOf("LT;", probe.call_bump));
    const uint16_t kCounterIdx =
        static_cast<uint16_t>(index_builder.FieldIndexOf("LT;", probe.counter));

    Specs s;
    s.ctor.code = {Op11x(kOpReturnVoid, 0)};
    s.ctor.registers_size = 1;
    s.ctor.ins_size = 1;

    // void javaWait() { waitForSetup(); }
    {
        std::vector<uint16_t> c;
        Op35c(&c, kOpInvokeStatic, kWaitIdx, {});
        c.push_back(Op11x(kOpReturnVoid, 0));
        s.java_wait.code = c;
        s.java_wait.registers_size = 1;
        s.java_wait.outs_size = 1;
    }
    // void javaSignal() { signalSetup(); }
    {
        std::vector<uint16_t> c;
        Op35c(&c, kOpInvokeStatic, kSignalIdx, {});
        c.push_back(Op11x(kOpReturnVoid, 0));
        s.java_signal.code = c;
        s.java_signal.registers_size = 1;
        s.java_signal.outs_size = 1;
    }
    // void javaCallback() { callBack(); }
    {
        std::vector<uint16_t> c;
        Op35c(&c, kOpInvokeStatic, kCallBackIdx, {});
        c.push_back(Op11x(kOpReturnVoid, 0));
        s.java_callback.code = c;
        s.java_callback.registers_size = 1;
        s.java_callback.outs_size = 1;
    }
    // void javaBumpOuter() { callBump(); }  — Java -> native -> Java -> static field
    {
        std::vector<uint16_t> c;
        Op35c(&c, kOpInvokeStatic, kCallBumpIdx, {});
        c.push_back(Op11x(kOpReturnVoid, 0));
        s.java_bump_outer.code = c;
        s.java_bump_outer.registers_size = 1;
        s.java_bump_outer.outs_size = 1;
    }
    // void bump() { counter = counter + 1; }
    //
    // A read-modify-write in bytecode, so two unsynchronised interpreters lose increments.
    {
        std::vector<uint16_t> c;
        Op21c(&c, kOpSget, 0, kCounterIdx);
        Op22b(&c, kOpAddIntLit8, 0, 0, 1);
        Op21c(&c, kOpSput, 0, kCounterIdx);
        c.push_back(Op11x(kOpReturnVoid, 0));
        s.bump.code = c;
        s.bump.registers_size = 1;
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
    if (!linker.AddDexFile(dex.data(), dex.size(), "vmlock.dex", &error)) {
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
        char n1[] = "waitForSetup";
        char n2[] = "signalSetup";
        char n3[] = "callBack";
        char n4[] = "callBump";
        char sig[] = "()V";
        const JNINativeMethod natives[] = {
            {n1, sig, reinterpret_cast<void*>(&WaitForSetup)},
            {n2, sig, reinterpret_cast<void*>(&SignalSetup)},
            {n3, sig, reinterpret_cast<void*>(&CallBackIntoJava)},
            {n4, sig, reinterpret_cast<void*>(&CallBackAndBump)},
        };
        Check(jni.RegisterNatives(t, natives, 4) == JNI_OK, "RegisterNatives");
    }

    kudroid::kuart::DexMethod* java_wait = t->FindDirectMethod("javaWait", "()V");
    kudroid::kuart::DexMethod* java_signal = t->FindDirectMethod("javaSignal", "()V");
    kudroid::kuart::DexMethod* java_callback = t->FindDirectMethod("javaCallback", "()V");
    kudroid::kuart::DexMethod* java_bump_outer = t->FindDirectMethod("javaBumpOuter", "()V");
    kudroid::kuart::DexMethod* bump = t->FindDirectMethod("bump", "()V");
    kudroid::kuart::DexMethod* plain = t->FindDirectMethod("plain", "()I");
    Check(java_wait != nullptr && java_signal != nullptr && java_callback != nullptr &&
              java_bump_outer != nullptr && bump != nullptr && plain != nullptr,
          "test methods resolved");
    if (java_wait == nullptr || java_signal == nullptr || java_callback == nullptr ||
        java_bump_outer == nullptr || bump == nullptr || plain == nullptr) {
        std::printf("=== FAILED ===\n");
        return 1;
    }

    Check(kudroid::kuart::VmLockDepth() == 0, "the VM lock starts unheld");

    // ── the deadlock, or its absence ────────────────────────────────────────
    //
    // Thread A runs Java that calls a blocking native method. Thread B runs Java that
    // releases it. With the VM lock held across the native call, B cannot start and A
    // waits out its timeout — which is the device behaviour, reproduced.
    std::printf("-- a blocking native method, released by another Java thread --\n");
    {
        std::thread waiter([&] { interp.Execute(java_wait, nullptr, 0); });

        // Let A get into the native method before B tries to run. Bounded, so a failure
        // to enter is reported rather than waited on forever.
        const auto deadline = std::chrono::steady_clock::now() + kTimeout;
        while (!g_native_entered.load() && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        Check(g_native_entered.load(), "the native method was entered");

        // The reading that matters: the lock is already released while native code runs.
        Check(g_depth_inside_native.load() == 0,
              "native code runs with the VM lock released (Android's kNative state)");

        // B must be able to interpret bytecode while A sits in native code. This is the
        // call that could not happen on device.
        std::atomic<bool> signalled{false};
        std::thread signaller([&] {
            interp.Execute(java_signal, nullptr, 0);
            signalled.store(true);
        });
        signaller.join();
        Check(signalled.load(), "another Java thread ran bytecode while native code blocked");

        waiter.join();
        Check(!g_native_timed_out.load(),
              "the blocking native method was released, not timed out");
    }

    // ── native code calling back into Java ─────────────────────────────────
    std::printf("-- native code re-entering the interpreter --\n");
    {
        g_interp = &interp;
        g_plain_method = plain;
        interp.Execute(java_callback, nullptr, 0);
        Check(g_callback_result.load() == 7,
              "a native method can call back into Java and get the right answer");
        // Back to released once the callback returns: the guard belonged to the callback,
        // not to the native method.
        Check(g_depth_during_callback.load() == 0,
              "the callback's lock is scoped to the callback");
        Check(kudroid::kuart::VmLockDepth() == 0, "the VM lock is unheld afterwards");
    }

    // A callback that WRITES shared state, run concurrently.
    //
    // The read-only callback above passes even with no lock at all, so it cannot tell a
    // correctly-locked callback from an unlocked one. This can: `counter = counter + 1` is
    // a read-modify-write in bytecode, and the path is Java -> native -> Java, so a guard
    // keyed on the interpreter's call depth is skipped and the increments race.
    std::printf("-- a callback that mutates shared Java state --\n");
    {
        g_bump_method = bump;
        constexpr int kThreads = 8;
        constexpr int kBumps = 250;

        std::vector<std::thread> threads;
        for (int i = 0; i < kThreads; ++i) {
            threads.emplace_back([&] {
                for (int c = 0; c < kBumps; ++c) {
                    interp.Execute(java_bump_outer, nullptr, 0);
                }
            });
        }
        for (auto& th : threads) th.join();

        kudroid::kuart::DexField* counter_field = t->FindStaticField("counter", "I");
        Check(counter_field != nullptr, "the counter field resolved");
        const int32_t seen = counter_field != nullptr
                                 ? t->static_values[counter_field->offset_or_slot].i
                                 : -1;
        Check(seen == kThreads * kBumps,
              "no increments lost through native -> Java callbacks (" +
                  std::to_string(seen) + " of " + std::to_string(kThreads * kBumps) + ")");
        Check(kudroid::kuart::VmLockDepth() == 0, "the VM lock is unheld afterwards");
    }

    // ── VmLockDepth() reports what is held ─────────────────────────────────
    //
    // The re-entry above is decided from this counter, so it has to be right. It used to
    // keep its old value while the lock was released, which made a thread holding nothing
    // answer "yes, I hold it" — and the re-entering call then ran with no lock at all.
    std::printf("-- the depth counter tracks what is actually held --\n");
    {
        Check(kudroid::kuart::VmLockDepth() == 0, "zero at rest");
        {
            kudroid::kuart::VmLockGuard guard;
            Check(kudroid::kuart::VmLockDepth() == 1, "one inside a guard");
            {
                kudroid::kuart::VmLockGuard nested;
                Check(kudroid::kuart::VmLockDepth() == 2, "two inside a nested guard");
            }
            Check(kudroid::kuart::VmLockDepth() == 1, "back to one after the nested guard");
            {
                kudroid::kuart::VmLockRelease released;
                Check(kudroid::kuart::VmLockDepth() == 0,
                      "zero while released — the lock is genuinely not held");
            }
            Check(kudroid::kuart::VmLockDepth() == 1, "restored after the release");
        }
        Check(kudroid::kuart::VmLockDepth() == 0, "zero again once the guard is gone");
    }

    // A release inside a guard must let another thread take the lock for real. If the
    // counter were the only thing that changed, this would time out.
    std::printf("-- a released lock is available to another thread --\n");
    {
        kudroid::kuart::VmLockGuard guard;
        std::atomic<bool> other_got_it{false};
        {
            kudroid::kuart::VmLockRelease released;
            std::thread other([&] {
                kudroid::kuart::VmLockGuard theirs;
                other_got_it.store(true);
            });
            other.join();
        }
        Check(other_got_it.load(), "another thread acquired the lock while it was released");
        Check(kudroid::kuart::VmLockDepth() == 1, "this thread has it back");
    }

    // Concurrent Java threads still serialise. Releasing around native calls must not have
    // turned the VM lock into no lock: the counter is unguarded, so an unsynchronised
    // interpreter would lose increments.
    std::printf("-- concurrent Java threads remain serialised --\n");
    {
        constexpr int kThreads = 8;
        constexpr int kCalls = 200;
        std::vector<std::thread> threads;
        std::atomic<int> total{0};
        for (int i = 0; i < kThreads; ++i) {
            threads.emplace_back([&] {
                for (int c = 0; c < kCalls; ++c) {
                    const DexValue r = interp.Execute(plain, nullptr, 0);
                    total.fetch_add(r.i);
                }
            });
        }
        for (auto& th : threads) th.join();
        Check(total.load() == kThreads * kCalls * 7,
              "every concurrent call returned the right value");
        Check(kudroid::kuart::VmLockDepth() == 0, "no lock left held by the main thread");
    }

    if (g_failures != 0) {
        std::printf("=== FAILED (%d error%s) ===\n", g_failures,
                    g_failures == 1 ? "" : "s");
        return 1;
    }
    std::printf("=== PASSED (0 error) ===\n");
    return 0;
}
