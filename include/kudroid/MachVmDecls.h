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

// VM_REGION_BASIC_INFO_64 asks the kernel for a region's current and MAXIMUM
// protection. max_protection is the honest answer to "can this page ever be
// executable here"; unlike a succeeded mprotect it is never silently clamped,
// and unlike executing the page it cannot kill the process. Layout mirrors
// XNU's vm_region_basic_info_data_64_t: five ints, a 64-bit offset (making the
// struct 40 bytes), then behavior and user_wired_count.
#define KUDROID_VM_REGION_BASIC_INFO_64_FLAVOR 9
#define KUDROID_VM_REGION_BASIC_INFO_64_COUNT 10
typedef struct kudroid_vm_region_basic_info_64 {
    vm_prot_t protection;
    vm_prot_t max_protection;
    vm_inherit_t inheritance;
    boolean_t shared;
    boolean_t reserved;
    memory_object_offset_t offset;
    vm_behavior_t behavior;
    unsigned short user_wired_count;
    unsigned short _kudroid_pad;
} kudroid_vm_region_basic_info_64_t;

#if defined(__APPLE__)
#if TARGET_OS_IPHONE && !TARGET_OS_OSX
#ifdef __cplusplus
extern "C" {
#endif
kern_return_t mach_vm_region(vm_map_t target_task,
                             mach_vm_address_t *address,
                             mach_vm_size_t *size,
                             vm_region_flavor_t flavor,
                             vm_region_info_t info,
                             mach_msg_type_number_t *info_count,
                             mach_port_t *object_name);
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
