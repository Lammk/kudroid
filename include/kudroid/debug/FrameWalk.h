// Frame-chain walking, in one place, with the stack bounds that make it safe.
//
// Why this exists. KuDroid had FOUR hand-written frame walks — the guard diagnostic in
// SyscallShim, two in the crash handler, and the blocking-wait registry's — and they did
// not agree about what a valid frame is. Three of them tested `fp > 0x1000 && fp <
// 0x7fffffffffff`, which excludes null and nothing else, and then followed `fp = *fp`
// without requiring the chain to go anywhere sensible.
//
// That is not a theoretical weakness. It killed a run:
//
//     signal = 11  si_code = 2  fault_addr = 0x100000000050
//     inst = 0xf9401d49                    -> LDR x9, [x10, #56]
//     x10  = 0x100000000018                -> 0x100000000018 + 56 == fault_addr
//     pc_sym: kudroid::(anonymous)::log_guard_acquire_diag+0x250
//     stack = [0x16b718000, 0x16b79b000)
//
// The walk had followed a slot that was not a saved frame pointer, landed on
// 0x100000000018 — which passes `> 0x1000 && < 0x7fffffffffff` — and dereferenced it 56
// bytes in. A diagnostic meant to explain a hang became the crash, on the guest's main
// thread, and the whole run was lost to it.
//
// The blocking-wait registry had already learned this and fixed it locally, with real
// bounds from pthread_get_stackaddr_np and a strictly-increasing chain requirement. The
// lesson did not travel to the other three copies. This file is that fix, once, for all
// of them: four call sites with different shapes but one definition of a valid frame.
//
// Everything here is async-signal-safe: no allocation, no locks, no libc beyond the
// pthread stack queries the crash handler already performs.
#ifndef KUDROID_DEBUG_FRAMEWALK_H
#define KUDROID_DEBUG_FRAMEWALK_H

#include <cstddef>
#include <cstdint>

namespace kudroid {

// A thread's stack, as [low, high). `valid` is false when the platform gave nothing, and
// a walk must then refuse to start rather than fall back to a plausibility test — that
// fallback is the bug this file removes.
struct StackBounds {
    uintptr_t low = 0;
    uintptr_t high = 0;
    bool valid = false;
};

// The calling thread's real bounds, queried fresh.
//
// Two pthread calls on Darwin, both safe in a signal handler, which is why the crash
// handler can use this. On Linux pthread_getattr_np allocates, so a hot path should
// prefer the cached form below.
StackBounds query_thread_stack_bounds();

// The same, cached per thread. For paths that ask repeatedly — the blocking-wait
// registry queries on every wait — where the allocation inside pthread_getattr_np would
// otherwise be paid each time.
//
// NOT for use from a signal handler on first call for a thread: the initialisation is
// lazy, and a first call inside a handler would run it there.
const StackBounds& cached_thread_stack_bounds();

// A cursor over an AAPCS64 / SysV frame chain.
//
// Both ABIs store [saved fp, return address] at the frame base, so one walker serves
// arm64 and x86-64. Construction validates the starting frame; every accessor is
// bounds-checked; and `next()` refuses a chain that does not move towards higher
// addresses, which makes a cycle impossible rather than merely bounded.
//
// Usage:
//     FrameWalker w(fp, bounds);
//     for (; w.valid(); w.next()) {
//         uintptr_t ret = 0;
//         if (w.return_address(&ret)) { ... }
//     }
class FrameWalker {
public:
    // `start_fp` is typically __builtin_frame_address(0), or the fp out of a ucontext.
    FrameWalker(uintptr_t start_fp, const StackBounds& bounds);

    // Whether the cursor currently points at a frame that may be read. False from the
    // start when the bounds are unknown or `start_fp` is not inside them — the case the
    // old plausibility checks let through.
    bool valid() const { return valid_; }

    uintptr_t fp() const { return fp_; }

    // How many times next() has advanced. Useful in a report: depth distinguishes "this
    // frame faulted" from "it called several levels down and that faulted".
    int depth() const { return depth_; }

    // The return address at [fp + 8]. False when out of bounds or zero, zero being how
    // the outermost frame terminates a chain.
    bool return_address(uintptr_t* out) const;

    // The saved frame pointer at [fp + 0], without advancing.
    bool saved_fp(uintptr_t* out) const;

    // An arbitrary word at [fp + byte_offset], read only if the WHOLE word lies inside
    // the stack. This is what the guard diagnostic needs — it reads [fp+56], where the
    // compiler spills x19 — and reading it unchecked is precisely what crashed.
    bool slot(std::size_t byte_offset, uintptr_t* out) const;

    // Advance to the caller's frame. Returns false, and leaves the cursor invalid, when
    // the chain ends or the next frame fails validation.
    //
    // A frame chain grows towards HIGHER addresses. Requiring that strictly is what stops
    // a garbage slot — the value read where a saved fp would be when the compiler omitted
    // the frame pointer at -O3 — from being followed anywhere at all.
    bool next();

private:
    bool frame_is_readable(uintptr_t candidate) const;

    StackBounds bounds_;
    uintptr_t fp_ = 0;
    int depth_ = 0;
    bool valid_ = false;
};

}  // namespace kudroid

#endif  // KUDROID_DEBUG_FRAMEWALK_H
