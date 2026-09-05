#pragma once

#include <cstddef>
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

// initialize main thread tls for android compatibility
void bionic_init_main_thread_tls(void);

// directly block android's dlopen — gpu libraries (libvulkan.so, libglesv2.so,
// libegl.so) returns a pseudo handle and resolves symbols straight to the ios root.
void* bionic_dlopen(const char* filename, int flags);
void* bionic_dlsym(void* handle, const char* symbol);

// Register the guest module's PT_TLS template (from elf_loader after mapping).
// Per-thread TLS blocks are copied from this template into place.
void kudroid_tls_set_template(const void* tls_template, size_t tls_filesz);

// The distance (relative to the guest's thread pointer) where the TLS module template is located
// for R_AARCH64_TLS_TPREL64 / TLS_DTPREL64 relocations.
size_t kudroid_tls_module_offset(void);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

namespace kudroid {

// Resolve an android/bionic symbol to a compatible ios/posix implementation.
// Unknown symbols return a non-null stub and emit a warning.
void* resolve_bionic_symbol(const char* name);

// True when `address` is the universal dummy — the stub every unresolved symbol is
// bound to, which takes no arguments and returns 0.
// A dummy-bound lookup must not read as success.
bool is_universal_dummy(const void* address);

// Clear diagnostic messages produced by the shim layer.
void bionic_shim_reset_trace();
const char* bionic_shim_trace();

// Append to the shared trace buffer (single thread_local).
// Every shim must call this instead of keeping a private buffer.
void trace_shim(const char* message);

// Handle SIGTRAP from mrs tpidr_el0 reads and AOT code.
// Returns true if handled.
bool bionic_handle_tpidr_trap(void* ucontext);

} // namespace kudroid

#endif

