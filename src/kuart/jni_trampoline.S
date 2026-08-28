// AAPCS64 / SysV-x86-64 trampoline for calling guest JNI natives.
//
// Why this exists: DexJniEnv used to call natives through Fn0..Fn6 casts taking
// only uintptr_t, which passes everything in general-purpose registers. Both
// AAPCS64 and SysV x86-64 pass float/double in a SEPARATE register file
// (v0-v7 / xmm0-xmm7), so a native declared as `jfloat f(JNIEnv*, jobject,
// jfloat)` read v0 while the value sat in x1 — and an F/D return sat in v0 while
// only x0 was read back. The old code detected the argument case and bailed out,
// but the RETURN case silently produced 0. For a graphics/game workload where
// float is most of the ABI surface, that was the worst kind of bug: wrong values,
// no diagnostic.
//
// Contract:
//   uint64_t kudroid_jni_call(const void* fn,
//                             const uint64_t* gp, unsigned ngp,
//                             const uint64_t* fp, unsigned nfp,
//                             uint64_t* fp_ret);
//
// `gp` and `fp` must each point at 8 initialised slots. All 8 of each are loaded
// unconditionally so the trampoline needs no branching on the counts; passing
// unused registers is harmless because JNI natives are never variadic. `ngp` and
// `nfp` are accepted for callers to validate against and for future stack-passing
// support, but are not read here.
//
// Float encoding: a `float` argument lives in the low 32 bits of its FP slot,
// which is exactly what `ldr dN` / `movsd xmmN` produce for s0/low-xmm reads.
// The FP return is written to *fp_ret as raw 64 bits; the caller bit-casts the
// low 32 bits for 'F' and all 64 for 'D'.
//
// Registers-only by design: stack-passed arguments are deliberately NOT handled.
// Apple's arm64 ABI packs stack arguments at their natural size rather than in
// uniform 8-byte slots (unlike AAPCS64 proper), so a stack `jint` occupies 4
// bytes there and 8 elsewhere. Getting that wrong corrupts arguments silently,
// which is the exact failure mode this file exists to remove. The caller rejects
// signatures that would need the stack instead of guessing.

#if defined(__APPLE__)
#define KUDROID_C_SYMBOL(name) _##name
#else
#define KUDROID_C_SYMBOL(name) name
#endif

#if defined(__aarch64__)

.text
.align 2
.globl KUDROID_C_SYMBOL(kudroid_jni_call)

KUDROID_C_SYMBOL(kudroid_jni_call):
    // x0=fn  x1=gp  x2=ngp  x3=fp  x4=nfp  x5=fp_ret
    stp     x29, x30, [sp, #-32]!
    mov     x29, sp
    str     x5, [sp, #16]           // fp_ret survives the call

    mov     x9, x0                  // callee address, before x0 is overwritten
    mov     x10, x1                 // gp base, before x1 is overwritten

    // FP first: x3 holds the fp base and is itself an argument register.
    ldp     d0, d1, [x3, #0]
    ldp     d2, d3, [x3, #16]
    ldp     d4, d5, [x3, #32]
    ldp     d6, d7, [x3, #48]

    ldp     x0, x1, [x10, #0]
    ldp     x2, x3, [x10, #16]
    ldp     x4, x5, [x10, #32]
    ldp     x6, x7, [x10, #48]

    blr     x9

    ldr     x9, [sp, #16]
    cbz     x9, 1f
    str     d0, [x9]                // raw bits of the FP return
1:
    ldp     x29, x30, [sp], #32
    ret

#elif defined(__x86_64__)

.text
.align 4
.globl KUDROID_C_SYMBOL(kudroid_jni_call)

KUDROID_C_SYMBOL(kudroid_jni_call):
    // rdi=fn  rsi=gp  rdx=ngp  rcx=fp  r8=nfp  r9=fp_ret
    pushq   %rbp
    movq    %rsp, %rbp
    subq    $16, %rsp               // keeps rsp 16-byte aligned at the call
    movq    %r9, -8(%rbp)           // fp_ret survives the call

    movq    %rdi, %r10              // callee address
    movq    %rsi, %r11              // gp base

    // FP first: rcx holds the fp base and is itself an argument register.
    movsd   0(%rcx), %xmm0
    movsd   8(%rcx), %xmm1
    movsd   16(%rcx), %xmm2
    movsd   24(%rcx), %xmm3
    movsd   32(%rcx), %xmm4
    movsd   40(%rcx), %xmm5
    movsd   48(%rcx), %xmm6
    movsd   56(%rcx), %xmm7

    movq    0(%r11), %rdi
    movq    8(%r11), %rsi
    movq    16(%r11), %rdx
    movq    24(%r11), %rcx
    movq    32(%r11), %r8
    movq    40(%r11), %r9

    movb    $8, %al                 // vector-register count; ignored by non-variadic callees
    call    *%r10

    movq    -8(%rbp), %r10
    testq   %r10, %r10
    je      1f
    movsd   %xmm0, (%r10)           // raw bits of the FP return
1:
    movq    %rbp, %rsp
    popq    %rbp
    ret

#endif

#if defined(__linux__) && (defined(__aarch64__) || defined(__x86_64__))
.section .note.GNU-stack,"",%progbits
#endif
