// Mach VM userland API.
//
// On macOS these live in <mach/mach_vm.h>. iOS SDKs mark that header
// unsupported for userland even though the symbols ship in libsystem_kernel,
// so the prototypes are declared directly for iOS-family targets. Types come
// from <mach/mach.h> on both.
#ifndef KUDROID_MACHVMDECLS_H
#define KUDROID_MACHVMDECLS_H

#include <mach/mach.h>
#include <TargetConditionals.h>

#if defined(__APPLE__)
#if TARGET_OS_IPHONE && !TARGET_OS_OSX
#ifdef __cplusplus
extern "C" {
#endif
kern_return_t mach_vm_remap(vm_map_t target_task,
                            mach_vm_address_t *target_address,
                            mach_vm_size_t size,
                            mach_vm_offset_t mask,
                            int flags,
                            vm_map_t src_task,
                            mach_vm_address_t src_address,
                            boolean_t copy,
                            vm_prot_t *cur_protection,
                            vm_prot_t *max_protection,
                            vm_inherit_t inheritance);
kern_return_t mach_vm_protect(vm_map_t target_task,
                              mach_vm_address_t address,
                              mach_vm_size_t size,
                              boolean_t set_maximum,
                              vm_prot_t new_protection);
kern_return_t mach_vm_deallocate(vm_map_t target_task,
                                 mach_vm_address_t address,
                                 mach_vm_size_t size);
#ifdef __cplusplus
}
#endif
#else
#include <mach/mach_vm.h>
#endif
#endif  // __APPLE__

#endif  // KUDROID_MACHVMDECLS_H
