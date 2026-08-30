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
// `firstFpIndex` is the same for the floating-point register file. It is 0 for every
// direct trampoline, because none of the shimmed variadic functions takes a fixed
// floating-point argument — but it is NOT 0 when the capture was rebuilt from a guest
// va_list, where the guest may already have consumed v-registers before handing the list
// over.
//
// Writes at most `size` bytes including the terminator, and returns the length the result
// WOULD have had — the same contract as snprintf, because callers size a second buffer
// from the return value and a truncated length makes them allocate too little. `size` of
// zero writes nothing at all, not even a terminator.
size_t FormatGuestVarargs(char* out, size_t size, const char* format,
                          const GuestVarargs* registers, unsigned firstGpIndex,
                          unsigned firstFpIndex = 0);

// A guest's va_list, as AAPCS64 defines it (§B.4, "va_list type").
//
// This is the second half of the same ABI split the trampolines exist for, and the one
// that is easier to miss. Apple's arm64 va_list is a plain `char*` cursor into the stack,
// passed BY VALUE. AAPCS64's is this 32-byte composite, and because it is a composite it
// is passed BY REFERENCE — the caller hands over a POINTER to it.
//
// So when a guest calls __android_log_vprint(prio, tag, fmt, ap), the shim receives a
// pointer to this struct in the register where the host expects a stack cursor. Forwarding
// it to the host's vsnprintf makes the host read the struct's own bytes as characters and
// pointers: `stack` is the first field, so the first thing printed is the low bytes of a
// stack address. That is the "Unable to locate asset: H,\x98B\x01" line on device — five
// bytes of a pointer, formatted as a filename, for an asset whose real name was never
// printed at all.
//
// The negative offsets are how AAPCS64 tracks consumption: `gr_top` and `vr_top` point
// just PAST the end of their save areas, and the offsets count backwards from there, so
// they reach zero exactly when the register file is exhausted.
struct GuestVaList {
    const void* stack;    // next argument in the overflow area
    const void* gr_top;   // one past the end of the x0-x7 save area
    const void* vr_top;   // one past the end of the q0-q7 save area
    int32_t gr_offs;      // byte offset from gr_top to the next unused x-register (<= 0)
    int32_t vr_offs;      // byte offset from vr_top to the next unused q-register (<= 0)
};

// Rebuild a register capture from a guest va_list.
//
// `guest_ap` is the pointer the guest passed, i.e. a `const GuestVaList*`. The save areas
// it points at were written by the guest's own va_start, so this copies them into the
// layout the formatter already understands rather than teaching the formatter a second
// argument source.
//
// On success `firstGpIndex` and `firstFpIndex` are set to where the guest had got to, and
// the result can be handed to FormatGuestVarargs unchanged. Returns false when `guest_ap`
// is null or the offsets are not ones va_start could have produced — a guest that passes
// a stale or fabricated va_list must not send the formatter walking arbitrary memory.
bool GuestVarargsFromVaList(const void* guest_ap, GuestVarargs* out,
                            unsigned* firstGpIndex, unsigned* firstFpIndex);

// Scan `format` from a character source, storing through the guest's output pointers.
//
// The same ABI split as formatting, with the consequences reversed. A guest's sscanf passes
// OUTPUT POINTERS as its varargs — the first six in x2-x7, the rest on the stack — and
// Apple's callee reads every one of them from the stack. So a forwarded call does not merely
// read the wrong place, it WRITES to it.
//
// On device Minecraft parses a UUID with eleven output pointers; the host wrote through five
// stack words that were never pointers. One held a jobject, which the next JNI call reported
// as clazz=0xf8ec9809 — a value no heap pointer can take. The following store went to
// address 0 and took SIGSEGV.
//
// `peek` returns the next character without consuming it, or -1 at end of input; `advance`
// consumes the character last peeked. Two callbacks rather than a buffer so fscanf can read
// a FILE* without this file including <cstdio> — it is compiled freestanding for the arm64
// test.
//
// Returns the number of items ASSIGNED, or -1 when input ended before any assignment. That
// distinction is part of the contract: a caller deciding whether to retry reads it.
int ScanGuestVarargsFrom(int (*peek)(void*), void (*advance)(void*), void* state,
                         const char* format, const GuestVarargs* registers,
                         unsigned firstGpIndex, unsigned firstFpIndex);

// Unpack a guest's variadic arguments into an array of jvalue according to a Dex shorty.
//
// `shorty` is the method's shorty signature (e.g. "VI" for void(int), "VL" for void(Object)).
// `firstGpIndex` is 3 for Call<Type>Method and CallStatic<Type>Method, 4 for CallNonvirtual<Type>Method.
// `firstFpIndex` is 0.
bool UnpackGuestVarargsToJvalues(const char* shorty, const GuestVarargs* registers,
                                unsigned firstGpIndex, unsigned firstFpIndex,
                                void* outJvalues, size_t maxOut);

bool UnpackGuestVaListToJvalues(const char* shorty, const void* guest_ap,
                               void* outJvalues, size_t maxOut);

} // namespace kudroid

// Format a guest's va_list, for the shims that receive one.
extern "C" size_t kudroid_format_guest_va_list(char* out, size_t size, const char* format,
                                               const void* guest_ap);

// Scan a string using a guest's va_list, for vsscanf. Returns items assigned, or -1.
extern "C" int kudroid_scan_guest_va_list(const char* input, const char* format,
                                          const void* guest_ap);

#if defined(__aarch64__)
// Handlers for the scanf trampolines in bionic_log_trampoline.S. `frame` points at the
// register capture the trampoline wrote.
extern "C" int kudroid_sscanf_from_registers(const uint64_t* frame);
extern "C" int kudroid_isoc99_sscanf_from_registers(const uint64_t* frame);
#endif

#endif // KUDROID_ABI_GUESTVARARGS_H
