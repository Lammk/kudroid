#if defined(__aarch64__)
/* Variadic-call trampolines for guest libc functions.
 *
 * A guest .so is compiled for Linux AAPCS64, where the first eight integer varargs
 * arrive in x0-x7 and the first eight floating-point varargs in v0-v7. Apple's arm64 ABI
 * passes *every* variadic argument on the stack. So a guest's variadic call cannot be
 * forwarded to the host implementation of the same function: the host reads the stack and
 * finds whatever happened to be there, which is how snprintf came to report a check
 * counter of 1839197616 and a block size of 4337540760.
 *
 * The way across is to capture the registers as the guest left them and format from that
 * capture. These trampolines do the capture; the C functions they call do the formatting.
 *
 * Layout of the frame, which the C side indexes as an array of uint64_t starting at
 * sp+16 — keep the two in step:
 *
 *   sp+0    x29, x30                     (frame record)
 *   sp+16   x0..x7                       registers[0..7]
 *   sp+80   caller's SP at the call      registers[8]   -> stack overflow area
 *   sp+88   (padding, keeps q0 aligned)  registers[9]
 *   sp+96   q0..q7                       registers[10..], one double per 16 bytes
 *
 * The caller's SP is where the stack overflow area begins: arguments past the eighth
 * integer or eighth float are there, in declaration order.
 */
.text
.align 2
#if defined(__APPLE__)
#define KUDROID_C_SYMBOL(name) _##name
#else
#define KUDROID_C_SYMBOL(name) name
#endif

/* Capture x0-x7, q0-q7 and the overflow pointer, then tail into `handler` with a pointer
 * to the capture in x0. The handler's return value in x0 becomes ours, which is what lets
 * snprintf report its length and __android_log_print its status. */
.macro KUDROID_VARARG_TRAMPOLINE name, handler
.globl KUDROID_C_SYMBOL(\name)
#if defined(__APPLE__)
.private_extern KUDROID_C_SYMBOL(\handler)
#endif
KUDROID_C_SYMBOL(\name):
    stp x29, x30, [sp, #-224]!
    stp x0, x1, [sp, #16]
    stp x2, x3, [sp, #32]
    stp x4, x5, [sp, #48]
    stp x6, x7, [sp, #64]
    add x9, sp, #224
    str x9, [sp, #80]
    stp q0, q1, [sp, #96]
    stp q2, q3, [sp, #128]
    stp q4, q5, [sp, #160]
    stp q6, q7, [sp, #192]
    add x0, sp, #16
    bl KUDROID_C_SYMBOL(\handler)
    ldp x29, x30, [sp], #224
    ret
.endm

KUDROID_VARARG_TRAMPOLINE kudroid_android_log_print_trampoline, kudroid_android_log_print_from_registers
KUDROID_VARARG_TRAMPOLINE kudroid_snprintf_trampoline, kudroid_snprintf_from_registers
KUDROID_VARARG_TRAMPOLINE kudroid_sprintf_trampoline, kudroid_sprintf_from_registers
KUDROID_VARARG_TRAMPOLINE kudroid_log_assert_trampoline, kudroid_log_assert_from_registers
KUDROID_VARARG_TRAMPOLINE kudroid_snprintf_chk_trampoline, kudroid_snprintf_chk_from_registers
KUDROID_VARARG_TRAMPOLINE kudroid_sprintf_chk_trampoline, kudroid_sprintf_chk_from_registers
#endif
