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

// VM_REGION_EXTENDED_INFO adds what basic info cannot say: whether the region is
// backed by a file pager (external_pager) and how it is shared. Mirrors XNU's
// vm_region_extended_info: seven ints, a short, two bytes, one more int (36 bytes).
#define KUDROID_VM_REGION_EXTENDED_INFO_FLAVOR 13
#define KUDROID_VM_REGION_EXTENDED_INFO_COUNT 9
typedef struct kudroid_vm_region_extended_info {
    vm_prot_t protection;
    unsigned int user_tag;
    unsigned int pages_resident;
    unsigned int pages_shared_now_private;
    unsigned int pages_swapped_out;
    unsigned int pages_dirtied;
    unsigned int ref_count;
    unsigned short shadow_depth;
    unsigned char external_pager;
    unsigned char share_mode;
    unsigned int pages_reusable;
} kudroid_vm_region_extended_info_t;

#if defined(__APPLE__)
#if TARGET_OS_IPHONE && !TARGET_OS_OSX
#ifdef __cplusplus
extern "C" {
#endif
kern_return_t mach_vm_allocate(vm_map_t target_task,
                               mach_vm_address_t *address,
                               mach_vm_size_t size,
                               int flags);
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
kern_return_t mach_vm_map(vm_map_t target_task,
                          mach_vm_address_t *address,
                          mach_vm_size_t size,
                          mach_vm_offset_t mask,
                          int flags,
                          mach_port_t port,
                          memory_object_offset_t offset,
                          boolean_t copy,
                          vm_prot_t cur_protection,
                          vm_prot_t max_protection,
                          vm_inherit_t inheritance);
#ifdef __cplusplus
}
#endif
#else
#include <mach/mach_vm.h>
#endif
#endif  // __APPLE__

#endif  // KUDROID_MACHVMDECLS_H
