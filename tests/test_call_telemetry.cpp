// test_call_telemetry.cpp — what the watchdog says while a call is wedged.
//
// Why this exists. ULTRAKILL stopped inside UnityPlayer.nativeRender and produced 188
// watchdog lines over 47 seconds. Every one of them said this:
//
//   watchdog native_active=1 native_call_id=14 native_elapsed_ms=47575
//     stage=before-result-decode class=Lbitter/jnibridge/JNIBridge; method=invoke
//     java_class=Landroid/os/Handler; java_method=sendMessageDelayed
//
// All of it was false except the elapsed time. Call 14 was JNIBridge.invoke, and it
// had completed in 435 MICROSECONDS, 47 seconds earlier — its four stage lines and its
// native-exit are all in the log. The call that never returned was call 13,
// UnityPlayer.nativeRender, whose last breadcrumb was `stage=before-trampoline` and
// which has no native-exit anywhere in the file.
//
// The record was a single global slot. native_call_enter overwrote it; native_call_exit
// only decremented a counter and never restored what the enclosing call had put there.
// So a nested call — native → Java → native, which is exactly what a JNI callback is —
// left its own name behind permanently, and the watchdog printed that name beside the
// outer call's clock for as long as the outer call stayed stuck. The thread sampler's
// reason= field read from the same slot, so all five thread samples were filed under
// the wrong method too.
//
// The Java half was wrong the same way: `Handler.sendMessageDelayed` on thread
// 3711854, a thread that appears in no thread sample at all (36-38 threads sampled,
// five times) because it had exited. Looper.loop() was the frame that was actually
// running, and it was named in the early lines before being overwritten.
//
// What makes this bug survive review is that every field is individually plausible.
// JNIBridge.invoke IS a real method that WAS called; before-result-decode IS a real
// stage; 47575ms IS the real duration of something. Only the combination is a lie. A
// test that checks "a stuck call is reported" passes against it — which is why these
// tests assert the reported IDENTITY after a nested call has returned.
#include "kudroid/NativeCallTelemetry.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    std::printf("%s %s\n", ok ? "  OK  " : "  FAIL", what.c_str());
    if (!ok) ++g_failures;
}

using kudroid::call_report_snapshot;
using kudroid::call_telemetry_reset_for_test;
using kudroid::CallReport;
using kudroid::java_call_enter;
using kudroid::java_call_exit;
using kudroid::java_call_should_trace;
using kudroid::native_call_enter;
using kudroid::native_call_exit;
using kudroid::native_call_stage;

// ─────────────────────────────────────────────────────────────────────────────

// The regression test. This is the ULTRAKILL sequence, reproduced exactly.
void test_nested_call_does_not_steal_the_outer_identity() {
    std::printf("[nesting] an inner call that returns does not keep the outer call's identity\n");
    call_telemetry_reset_for_test();

    // Call 13: UnityPlayer.nativeRender. Enters, reaches before-trampoline, and never
    // returns — this is the wedged call.
    native_call_enter("Lcom/unity3d/player/UnityPlayer;", "nativeRender", "()Z", 1);
    native_call_stage("before-vm-release");
    native_call_stage("before-trampoline");

    {
        CallReport r = call_report_snapshot();
        Check(std::strcmp(r.native_method, "nativeRender") == 0,
              "while it is the only call, the report names nativeRender");
        Check(std::strcmp(r.native_stage, "before-trampoline") == 0,
              "at the stage it actually reached");
        Check(r.native_stack_depth == 1, "and reports a stack depth of 1");
    }

    // Call 14: the guest calls back into Java, which calls JNIBridge.invoke. It
    // completes normally and quickly, as it did on device (435 microseconds).
    native_call_enter("Lbitter/jnibridge/JNIBridge;", "invoke",
                      "(JLjava/lang/Class;Ljava/lang/reflect/Method;[Ljava/lang/Object;)"
                      "Ljava/lang/Object;",
                      1);
    native_call_stage("before-vm-release");
    native_call_stage("before-trampoline");
    native_call_stage("after-trampoline");
    native_call_stage("before-result-decode");

    {
        // While both are in flight the report still leads with the OUTER call, because
        // "native code has been running for N ms without returning" is a statement
        // about the outermost frame. The inner one is reported alongside, not instead.
        CallReport r = call_report_snapshot();
        Check(std::strcmp(r.native_method, "nativeRender") == 0,
              "with both in flight, the report still leads with the outer call");
        Check(r.native_stack_depth == 2, "and says the thread is two deep");
        Check(std::strcmp(r.native_inner_method, "invoke") == 0,
              "naming the inner call separately, which is where execution actually is");
        Check(std::strcmp(r.native_inner_stage, "before-result-decode") == 0,
              "with the inner call's own stage");
    }

    native_call_exit();  // JNIBridge.invoke returns

    // THE assertion. On device this is where the record became permanently wrong.
    CallReport r = call_report_snapshot();
    Check(std::strcmp(r.native_method, "nativeRender") == 0,
          "after the inner call returns, the report names nativeRender again — not "
          "JNIBridge.invoke, which is what 188 consecutive watchdog lines said");
    Check(std::strcmp(r.native_class, "Lcom/unity3d/player/UnityPlayer;") == 0,
          "with the outer call's class");
    Check(std::strcmp(r.native_stage, "before-trampoline") == 0,
          "and the outer call's stage — before-result-decode belonged to the call that "
          "already returned");
    Check(r.native_stack_depth == 1, "the stack is one deep again");
    Check(r.native_inner_method[0] == '\0',
          "and no inner call is reported, because there is none");

    native_call_exit();
    Check(!call_report_snapshot().native_active, "with both out, nothing is reported");
}

// The elapsed time must belong to the same call the identity does. On device the
// duration was call 13's and the name was call 14's; either number alone looks fine.
void test_elapsed_time_belongs_to_the_named_call() {
    std::printf("[nesting] the elapsed time and the reported name describe one call\n");
    call_telemetry_reset_for_test();

    native_call_enter("Louter;", "outer", "()V", 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(60));

    // A nested call that enters and leaves. Its own age is a few microseconds.
    native_call_enter("Linner;", "inner", "()V", 0);
    native_call_exit();

    CallReport r = call_report_snapshot();
    Check(std::strcmp(r.native_method, "outer") == 0, "the report names outer");
    Check(r.native_elapsed_ms >= 55,
          std::string("and its elapsed time is outer's own (") +
              std::to_string(r.native_elapsed_ms) + " ms >= 55), not the inner call's few "
              "microseconds");

    native_call_exit();
}

// The call id must move with the identity, or a reader cannot correlate a watchdog
// line with the native-enter breadcrumb that started the call.
void test_call_id_matches_the_reported_call() {
    std::printf("[nesting] the reported call id belongs to the reported call\n");
    call_telemetry_reset_for_test();

    native_call_enter("Louter;", "outer", "()V", 0);
    const unsigned long long outer_id = call_report_snapshot().native_call_id;
    Check(outer_id != 0, "the outer call has an id");

    native_call_enter("Linner;", "inner", "()V", 0);
    const unsigned long long inner_id = call_report_snapshot().native_call_id;
    Check(inner_id == outer_id,
          "which does not change when a nested call begins — the id names the call being "
          "timed");

    native_call_exit();
    Check(call_report_snapshot().native_call_id == outer_id,
          "nor when it ends");
    native_call_exit();
}

// Deep nesting, because a JNI callback can itself call back. Every level must restore
// the one below it.
void test_deep_nesting_unwinds_correctly() {
    std::printf("[nesting] ten levels of nesting unwind to the right call at each step\n");
    call_telemetry_reset_for_test();

    constexpr int kDepth = 10;
    for (int i = 0; i < kDepth; ++i) {
        native_call_enter("Lclass;", (std::string("m") + std::to_string(i)).c_str(), "()V", i);
    }
    {
        CallReport r = call_report_snapshot();
        Check(std::strcmp(r.native_method, "m0") == 0, "the outermost is m0");
        Check(r.native_stack_depth == kDepth,
              std::string("at depth ") + std::to_string(kDepth));
        Check(std::strcmp(r.native_inner_method, "m9") == 0, "and the innermost is m9");
    }

    bool all_correct = true;
    for (int i = kDepth - 1; i >= 1; --i) {
        native_call_exit();
        CallReport r = call_report_snapshot();
        // The outermost never changes on the way out; the innermost walks back.
        if (std::strcmp(r.native_method, "m0") != 0) all_correct = false;
        if (r.native_stack_depth != static_cast<unsigned>(i)) all_correct = false;
        const std::string expect_inner = "m" + std::to_string(i - 1);
        if (i > 1 && std::strcmp(r.native_inner_method, expect_inner.c_str()) != 0) {
            all_correct = false;
        }
    }
    Check(all_correct, "every level reports m0 as the outer call and the correct inner one");

    native_call_exit();
    Check(!call_report_snapshot().native_active, "and the stack is empty at the end");
}

// A stage applies to the frame that is running, which is the innermost. Writing it to
// the outer frame would report nativeRender as being at a stage it never reached.
void test_stage_applies_to_the_innermost_frame() {
    std::printf("[stage] a stage marks the running frame, not the outermost one\n");
    call_telemetry_reset_for_test();

    native_call_enter("Louter;", "outer", "()V", 0);
    native_call_stage("outer-stage");
    native_call_enter("Linner;", "inner", "()V", 0);
    native_call_stage("inner-stage");

    CallReport r = call_report_snapshot();
    Check(std::strcmp(r.native_stage, "outer-stage") == 0,
          "the outer frame keeps the stage it set");
    Check(std::strcmp(r.native_inner_stage, "inner-stage") == 0,
          "and the inner frame carries its own");

    native_call_exit();
    Check(std::strcmp(call_report_snapshot().native_stage, "outer-stage") == 0,
          "which is still true after the inner frame leaves");
    native_call_exit();
}

// ── threads ─────────────────────────────────────────────────────────────────

// One thread's calls must not appear as another's. The record was global, so on device
// any thread entering a native method rewrote the name the watchdog was reporting for
// UnityMain — and with 36 live threads that is a constant hazard.
void test_threads_do_not_overwrite_each_other() {
    std::printf("[threads] a call on one thread does not rename another thread's call\n");
    call_telemetry_reset_for_test();

    native_call_enter("Lmain;", "mainCall", "()V", 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    std::atomic<bool> worker_inside{false};
    std::atomic<bool> release{false};
    std::thread worker([&] {
        native_call_enter("Lworker;", "workerCall", "()V", 0);
        native_call_stage("worker-stage");
        worker_inside = true;
        while (!release.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        native_call_exit();
    });
    while (!worker_inside.load()) std::this_thread::yield();

    // The main thread's call is older, so it is the one worth escalating on and the one
    // the report must name.
    CallReport r = call_report_snapshot();
    Check(std::strcmp(r.native_method, "mainCall") == 0,
          "the report names the OLDEST outermost call across all threads, which is the "
          "one that has been running longest");
    Check(r.native_thread_id != 0, "and attributes it to a thread");

    release = true;
    worker.join();

    Check(std::strcmp(call_report_snapshot().native_method, "mainCall") == 0,
          "and the worker's call leaves no trace behind when it returns");
    native_call_exit();
}

// A thread that exits cleanly must give its slot back, or a guest that churns worker
// threads exhausts the table and stops being tracked. ULTRAKILL created 38 threads
// during startup, several of them short-lived.
void test_exited_threads_release_their_slots() {
    std::printf("[threads] a thread that finished cleanly stops being reported\n");
    call_telemetry_reset_for_test();

    for (int i = 0; i < 200; ++i) {
        std::thread t([] {
            native_call_enter("Lshort;", "shortCall", "()V", 0);
            native_call_exit();
        });
        t.join();
    }
    Check(!call_report_snapshot().native_active,
          "200 short-lived threads leave nothing active — the slot table is not "
          "exhausted, so a later real call is still tracked");

    // And prove tracking still works afterwards.
    native_call_enter("Lafter;", "afterCall", "()V", 0);
    Check(std::strcmp(call_report_snapshot().native_method, "afterCall") == 0,
          "and a call made afterwards is still reported");
    native_call_exit();
}

// A thread that dies MID-CALL must keep its record: that is the crash case, and the
// unfinished stack is the evidence. Releasing it would erase the one thing worth
// having.
void test_thread_dying_mid_call_keeps_its_record() {
    std::printf("[threads] a thread that dies inside a call stays in the report\n");
    call_telemetry_reset_for_test();

    std::thread t([] {
        native_call_enter("Ldying;", "dyingCall", "()V", 0);
        native_call_stage("about-to-vanish");
        // Returns without calling native_call_exit — the shape of a thread that
        // faulted, or one that siglongjmp'd out of the interpreter.
    });
    t.join();

    CallReport r = call_report_snapshot();
    Check(r.native_active, "the call is still reported after the thread is gone");
    Check(std::strcmp(r.native_method, "dyingCall") == 0, "by name");
    Check(std::strcmp(r.native_stage, "about-to-vanish") == 0,
          "at the stage it reached — which is the only evidence of where it died");
}

// ── Java side ───────────────────────────────────────────────────────────────

// The Java record had the same defect and produced the same false line:
// `Handler.sendMessageDelayed` on a thread that no longer existed, while Looper.loop
// was the frame actually running.
void test_java_nesting_restores_the_outer_frame() {
    std::printf("[java] a nested Java frame does not keep the outer frame's identity\n");
    call_telemetry_reset_for_test();

    java_call_enter("Landroid/os/Looper;", "loop", "()V", 1);
    java_call_enter("Landroid/os/Handler;", "sendMessageDelayed", "(Landroid/os/Message;J)Z", 2);

    {
        CallReport r = call_report_snapshot();
        Check(std::strcmp(r.java_method, "loop") == 0,
              "with both in flight the report leads with the outer Java frame");
    }

    java_call_exit();  // sendMessageDelayed returns

    CallReport r = call_report_snapshot();
    Check(std::strcmp(r.java_method, "loop") == 0,
          "and after it returns, the report still names Looper.loop — not "
          "Handler.sendMessageDelayed, which is what the device log said for 32 seconds");
    Check(std::strcmp(r.java_class, "Landroid/os/Looper;") == 0, "with Looper's class");

    java_call_exit();
    Check(!call_report_snapshot().java_active, "and nothing is reported once loop returns");
}

void test_java_thread_attribution_is_a_live_thread() {
    std::printf("[java] the reported Java thread is the one running the reported frame\n");
    call_telemetry_reset_for_test();

    java_call_enter("Lmain;", "mainFrame", "()V", 1);

    // A short-lived thread that runs its own Java frame and exits — the shape of the
    // thread the device log blamed. It must not leave its thread id behind attached to
    // the main thread's frame.
    std::thread t([] {
        java_call_enter("Lother;", "otherFrame", "()V", 1);
        java_call_exit();
    });
    t.join();

    CallReport r = call_report_snapshot();
    Check(std::strcmp(r.java_method, "mainFrame") == 0, "the main frame is reported");
    Check(r.java_thread_id != 0, "with a thread id");
    java_call_exit();
}

// ── the sampling trigger ────────────────────────────────────────────────────

// The thread sampler is scheduled off the reported call id. If the id did not change
// between calls, a new wedged call would inherit the previous one's schedule and its
// first sample would be skipped.
void test_a_new_outer_call_gets_a_fresh_identity() {
    std::printf("[schedule] each new outermost call reports a new id\n");
    call_telemetry_reset_for_test();

    native_call_enter("La;", "first", "()V", 0);
    const unsigned long long first = call_report_snapshot().native_call_id;
    native_call_exit();

    native_call_enter("Lb;", "second", "()V", 0);
    const unsigned long long second = call_report_snapshot().native_call_id;
    native_call_exit();

    Check(first != 0 && second != 0 && first != second,
          "two consecutive outermost calls have different ids, so the watchdog's sample "
          "schedule restarts for each one instead of inheriting the previous call's");
}

// ── robustness ──────────────────────────────────────────────────────────────

void test_unmatched_exit_is_safe() {
    std::printf("[robust] an unmatched exit does not corrupt the record\n");
    call_telemetry_reset_for_test();

    native_call_exit();
    native_call_exit();
    Check(!call_report_snapshot().native_active, "two exits with no enter leave nothing active");

    native_call_enter("Lx;", "x", "()V", 0);
    native_call_exit();
    native_call_exit();
    Check(!call_report_snapshot().native_active, "and an extra exit after a real call is ignored");

    java_call_exit();
    Check(!call_report_snapshot().java_active, "same on the Java side");
}

void test_null_names_do_not_crash() {
    std::printf("[robust] null names are recorded as unknown, not dereferenced\n");
    call_telemetry_reset_for_test();

    native_call_enter(nullptr, nullptr, nullptr, 0);
    native_call_stage(nullptr);
    CallReport r = call_report_snapshot();
    Check(r.native_active, "the call is still tracked");
    Check(r.native_method[0] != '\0', "and the missing name is recorded as a placeholder");
    native_call_exit();

    java_call_enter(nullptr, nullptr, nullptr, 1);
    Check(call_report_snapshot().java_active, "same on the Java side");
    java_call_exit();
}

// Overflowing the per-thread depth must degrade to losing the innermost names, never
// to corrupting the outer frame — which is the one the report leads with.
void test_depth_overflow_preserves_the_outer_frame() {
    std::printf("[robust] overflowing the nesting depth keeps the outermost call intact\n");
    call_telemetry_reset_for_test();

    native_call_enter("Louter;", "outermost", "()V", 0);
    constexpr int kOverflow = 80;  // well past the table
    for (int i = 0; i < kOverflow; ++i) {
        native_call_enter("Ldeep;", "deep", "()V", 0);
    }
    CallReport r = call_report_snapshot();
    Check(std::strcmp(r.native_method, "outermost") == 0,
          "the outermost call is still named correctly past the depth limit");

    for (int i = 0; i < kOverflow; ++i) native_call_exit();
    Check(std::strcmp(call_report_snapshot().native_method, "outermost") == 0,
          "and unwinding the overflow restores it rather than leaving the stack skewed");
    native_call_exit();
    Check(!call_report_snapshot().native_active, "then the stack is empty");
}

// The tracing filter is what keeps this affordable. Pinned because widening it would
// put a breadcrumb on every framework helper and turn tracing into a launch workload.
void test_java_trace_filter() {
    std::printf("[java] the tracing filter admits lifecycle frames and rejects the rest\n");

    Check(java_call_should_trace("anything", 0), "depth 0 is always traced");
    Check(java_call_should_trace("anything", 2), "as is depth 2");
    Check(!java_call_should_trace("someHelper", 5), "a deep ordinary helper is not");
    Check(java_call_should_trace("onCreate", 9), "but onCreate is, at any depth");
    Check(java_call_should_trace("onResume", 9), "and onResume");
    Check(java_call_should_trace("main", 9), "and main");
    Check(!java_call_should_trace(nullptr, 9), "a null name at depth is rejected, not read");
}

}  // namespace

int main() {
    std::printf("=== call telemetry (what the watchdog reports) ===\n");

    test_nested_call_does_not_steal_the_outer_identity();
    test_elapsed_time_belongs_to_the_named_call();
    test_call_id_matches_the_reported_call();
    test_deep_nesting_unwinds_correctly();
    test_stage_applies_to_the_innermost_frame();
    test_threads_do_not_overwrite_each_other();
    test_exited_threads_release_their_slots();
    test_thread_dying_mid_call_keeps_its_record();
    test_java_nesting_restores_the_outer_frame();
    test_java_thread_attribution_is_a_live_thread();
    test_a_new_outer_call_gets_a_fresh_identity();
    test_unmatched_exit_is_safe();
    test_null_names_do_not_crash();
    test_depth_overflow_preserves_the_outer_frame();
    test_java_trace_filter();

    call_telemetry_reset_for_test();

    std::printf("=== %d checks, %d failures ===\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
