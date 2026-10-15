// CrashInternal.h — seam for internals CrashHandling exposes to its
// immediate sibling (kudroid_bridge.cpp) only.
//
// Everything here has a single consumer outside CrashHandling.cpp; keeping it
// out of the public CrashHandling.h documents that and keeps accidental use
// from spreading.

#pragma once

#include <jni.h>

#include <string>

// Invoke JNI_OnLoad under signal protection. sigsetjmp is placed in a
// dedicated function to prevent longjmp from clobbering caller local
// variables. Returns 0 on normal return (*outVersion valid), -1 on C++
// exception, >0 on the trapped signal number.
int kudroid_call_jni_onload_guarded(jint (*fn)(JavaVM*, void*),
                                    JavaVM* vm, jint* outVersion);

// Render the state captured when the JNI_OnLoad guard swallowed a signal
// (registers, fault address, backtrace). Used by the guard's caller to report
// which library faulted and what it touched.
std::string describeJniGuardFault(const std::string& library, int guardRc);
