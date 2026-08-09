#pragma once

#include <cstddef>
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

// Main thread TLS initialization for Android compatibility
void bionic_init_main_thread_tls(void);

// Direct intercept for Android dlopen — GPU libs (libvulkan.so, libGLESv2.so,
// libEGL.so) return a fake handle and resolve symbols straight to iOS native.
void* bionic_dlopen(const char* filename, int flags);
void* bionic_dlsym(void* handle, const char* symbol);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

namespace kudroid {

/// Resolve an Android/Bionic symbol to an iOS/POSIX-compatible implementation.
/// Unknown symbols return a non-null dummy function and emit a warning.
void* resolve_bionic_symbol(const char* name);

/// Clear and retrieve diagnostic messages produced by the shim.
void bionic_shim_reset_trace();
const char* bionic_shim_trace();

/// Handles SIGTRAP caused by the AOT patched mrs x, tpidr_el0 instructions.
/// Returns true if handled successfully.
bool bionic_handle_tpidr_trap(void* ucontext);

} // namespace kudroid

#endif

