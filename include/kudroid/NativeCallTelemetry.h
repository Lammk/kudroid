#pragma once

#include <cstddef>
#include <cstdint>

namespace kudroid {

void native_run_begin();
void native_phase(const char* phase);

// A native call can outlive every crash handler when the OS terminates the
// process. These hooks maintain a small, durable diagnostic record for that
// case. The implementation is intentionally independent of any guest app.
//
// Calls NEST: a native method re-enters Java, which calls another native method.
// Each of these pushes onto the calling thread's own stack, so the record of the
// outer call survives the inner one returning. Getting that wrong is not a cosmetic
// problem — see the comment on call_report_snapshot().
void native_call_enter(const char* class_name, const char* method,
                       const char* signature, int vm_lock_depth);
void native_call_stage(const char* stage);
void native_call_exit();

// Frames currently inside UnityPlayer.nativeRender. Teardown waits for zero
// before pulling the GPU layer so no present lands on an unbound surface.
int native_frame_in_flight();

// Tell the watchdog a thread has taken a fatal signal.
//
// After this, a native call that never returns is a CRASH, not a hang, and the
// watchdog must stop describing it as one. The captured ULTRAKILL log is the case:
// UnityMain faulted, was parked in the crash handler, and the watchdog went on
// printing `native_elapsed_ms=16570 stage=before-trampoline nativeRender` every
// 250ms — so the evidence pointed at a deadlock in a native call that had in fact
// already died, and that is what got investigated.
//
// Called from a signal handler, so the implementation may only store scalars.
void native_note_fatal_signal(int signal_number, unsigned long long thread_id);

// Track the shallow lifecycle portion of Java execution without logging every
// interpreter frame in a busy application.
bool java_call_should_trace(const char* method, size_t depth);
void java_call_enter(const char* class_name, const char* method,
                     const char* signature, size_t depth);
void java_call_exit();

// What the watchdog would report right now.
//
// This exists as a test seam because the bug it guards against is invisible from
// outside: every field the watchdog printed was individually plausible, and only
// the COMBINATION was wrong. ULTRAKILL wedged inside UnityPlayer.nativeRender
// (call 13), which called back into Java, which called JNIBridge.invoke (call 14).
// Call 14 returned in 435 microseconds. Because the record was a single global slot
// that native_call_exit() never restored, the watchdog then spent 47 seconds
// printing call 14's class, method and stage next to call 13's elapsed time — so
// the log named a method that had already returned as the thing that was hung, and
// the thread sampler inherited the same wrong name in its reason= field.
//
// A test that only checks "a stuck call is reported" passes against that bug. What
// catches it is asserting the reported identity after a NESTED call has returned.
struct CallReport {
    bool native_active = false;
    unsigned long long native_call_id = 0;
    unsigned long long native_thread_id = 0;
    unsigned long long native_elapsed_ms = 0;
    int native_vm_lock_depth = 0;
    // How many native calls are in flight on the reported thread. 1 means the
    // reported call is the only one; more means it called back into Java.
    unsigned native_stack_depth = 0;
    char native_class[256] = {};
    char native_method[256] = {};
    char native_signature[512] = {};
    char native_stage[64] = {};
    // The deepest call on the same thread, when it is not the reported one. This is
    // where execution actually is; the reported call is where it went in.
    char native_inner_class[256] = {};
    char native_inner_method[256] = {};
    char native_inner_stage[64] = {};

    bool java_active = false;
    unsigned long long java_thread_id = 0;
    unsigned long long java_elapsed_ms = 0;
    size_t java_depth = 0;
    char java_class[256] = {};
    char java_method[256] = {};
    char java_signature[512] = {};
};

CallReport call_report_snapshot();

// Test seam: drop all call records. Not for use on a live guest.
void call_telemetry_reset_for_test();

}  // namespace kudroid
