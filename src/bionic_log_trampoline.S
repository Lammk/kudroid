#if defined(__aarch64__)
.text
.align 2
#if defined(__APPLE__)
#define KUDROID_C_SYMBOL(name) _##name
.private_extern KUDROID_C_SYMBOL(kudroid_android_log_print_from_registers)
#else
#define KUDROID_C_SYMBOL(name) name
#endif
.globl KUDROID_C_SYMBOL(kudroid_android_log_print_trampoline)

KUDROID_C_SYMBOL(kudroid_android_log_print_trampoline):
    stp x29, x30, [sp, #-96]!
    stp x0, x1, [sp, #16]
    stp x2, x3, [sp, #32]
    stp x4, x5, [sp, #48]
    stp x6, x7, [sp, #64]
    add x9, sp, #96
    str x9, [sp, #80]
    add x0, sp, #16
    bl KUDROID_C_SYMBOL(kudroid_android_log_print_from_registers)
    ldp x29, x30, [sp], #96
    ret
#endif
