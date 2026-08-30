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

// Track the shallow lifecycle portion of Java execution without logging every
// interpreter frame in a busy application.
bool java_call_should_trace(const char* method, size_t depth);
void java_call_enter(const char* class_name, const char* method,
                     const char* signature, size_t depth);
void java_call_exit();

}  // namespace kudroid
