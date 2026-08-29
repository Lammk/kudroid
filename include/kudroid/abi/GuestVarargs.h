// Formatting a guest's variadic arguments.
//
// A guest .so is built for Linux AAPCS64: the first eight integer varargs arrive in
// x0-x7, the first eight floating-point varargs in v0-v7, and anything further on the
// stack. Apple's arm64 ABI passes every variadic argument on the stack instead. So a
// guest's variadic call cannot be forwarded to the host implementation of the same
// function — the host reads the stack, finds whatever was there, and formats it. That is
// not a crash: it produces plausible-looking output, which is how a shimmed snprintf came
// to report a check counter of 1839197616 and a filesystem block size of 4337540760.
//
// The way across is to capture the registers as the guest left them (the trampolines in
// bionic_log_trampoline.S) and format from that capture (here).
//
// Written without <string> or libc so the same code can be cross-compiled freestanding
// and run under qemu: the bug is invisible on an x86-64 host, where both the register
// files and the convention differ, so the only test that proves anything runs the real
// code on real arm64.
#ifndef KUDROID_ABI_GUESTVARARGS_H
#define KUDROID_ABI_GUESTVARARGS_H

#include <stddef.h>
#include <stdint.h>

namespace kudroid {

// A guest's argument registers, exactly as its variadic call left them.
//
// The field order IS the frame layout the trampolines write, so the two must be changed
// together. See bionic_log_trampoline.S.
struct GuestVarargs {
    uint64_t gp[8];             // x0..x7
    const uint64_t* overflow;   // caller's SP: arguments past the register files
    uint64_t reserved;          // keeps the q-register block 16-byte aligned
    uint64_t fp[16];            // q0..q7, low and high half of each
};

// Format `format` with the guest's arguments into `out`.
//
// `firstGpIndex` is the index of the first variadic argument: 3 for
// snprintf(buf, size, fmt, ...) and __android_log_print(prio, tag, fmt, ...), 2 for
// sprintf(buf, fmt, ...).
//
// Writes at most `size` bytes including the terminator, and returns the length the result
// WOULD have had — the same contract as snprintf, because callers size a second buffer
// from the return value and a truncated length makes them allocate too little. `size` of
// zero writes nothing at all, not even a terminator.
size_t FormatGuestVarargs(char* out, size_t size, const char* format,
                          const GuestVarargs* registers, unsigned firstGpIndex);

} // namespace kudroid

#endif // KUDROID_ABI_GUESTVARARGS_H
