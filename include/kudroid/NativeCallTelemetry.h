#pragma once

#include <cstddef>

namespace kudroid {

void native_run_begin();
void native_phase(const char* phase);

// A native call can outlive every crash handler when the OS terminates the
// process. These hooks maintain a small, durable diagnostic record for that
// case. The implementation is intentionally independent of any guest app.
void native_call_enter(const char* class_name, const char* method,
                       const char* signature, int vm_lock_depth);
void native_call_stage(const char* stage);
void native_call_exit();

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

}  // namespace kudroid
