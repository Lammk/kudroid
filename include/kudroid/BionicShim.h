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
// c c kh i TLS per-thread c  th  sao ch p n  v o  ng v  tr .
void kudroid_tls_set_template(const void* tls_template, size_t tls_filesz);

// The distance (relative to the guest's thread pointer) where the TLS module template is located
// trong kh i TLS per-thread. C ng gi  tr  n y  c elf_loader d ng l m bias
// cho relocations R_AARCH64_TLS_TPREL64 / TLS_DTPREL64.
size_t kudroid_tls_module_offset(void);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

namespace kudroid {

// / ph n gi i m t k  hi u android/bionic th nh m t tri n khai t ng th ch ios/posix.
// / c c k  hi u kh ng x c  nh tr  v  m t h m gi  kh c null v  ph t ra c nh b o.
void* resolve_bionic_symbol(const char* name);

// True when `address` is the universal dummy — the stub every unresolved symbol is
// bound to, which takes no arguments and returns 0.
//
// Needed because "resolved" and "bound to a stub that returns 0" are different
// answers that were being reported as the same one. resolve_bionic_symbol caches the
// dummy under the symbol's name, so every later lookup finds a non-null address and
// says so. In the ULTRAKILL log that produced consecutive lines that contradict each
// other:
//
//     [BionicShim] missing symbol bound to dummy: AChoreographer_getInstance
//     [KuDroidSyscall] bionic_dlsym: [AChoreographer_getInstance] resolved via guest
//                      LibraryManager
//
// Same address, and it was the dummy both times. The second line reads as a success
// and cancels the warning above it, so a search for what was missing finds nothing —
// which is why six absent frame-pacing symbols sat unnoticed in a log that named all
// of them.
//
// Binding to the dummy stays: a guest that calls a null pointer faults at 0x0, and a
// stub returning 0 is the lesser failure. What changes is that the log says so.
bool is_universal_dummy(const void* address);

// / x a v  truy xu t c c th ng b o ch n  o n  c t o ra b i l p  m.
void bionic_shim_reset_trace();
const char* bionic_shim_trace();

// / th m m t d ng v o b   m trace d ng chung (thread_local duy nh t).
// / m i shim (k  c  SyscallShim/AudioShim/...) ph i g i h m n y thay v
// / duy tr  bi n gShimTrace ri ng   n u kh ng bionic_shim_trace()  c
// / bi n kh c v  k t qu  lu n tr ng.
void trace_shim(const char* message);

// / x  l  sigtrap do c c l nh mrs x, tpidr_el0  c v  aot g y ra.
// / tr  v  true n u x  l  succeeded.
bool bionic_handle_tpidr_trap(void* ucontext);

} // namespace kudroid

#endif

