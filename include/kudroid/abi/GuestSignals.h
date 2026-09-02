// Guest signal handling: the guest's sigaction, translated rather than forwarded.
//
// The problem this solves. `struct sigaction` is not one layout — bionic declares a
// different field order for LP64 than for ILP32, and Darwin's differs from Linux's
// again. KuDroid declared the ILP32 order:
//
//     bionic LP64:  size=32  flags@0  handler@8  mask@16  restorer@24
//     KuDroid had:  size=32  flags@16 handler@0  mask@8   restorer@24
//
// Both are 32 bytes, so nothing ever failed a size check. What happened instead is
// that `sa_flags` was read as the handler pointer. ULTRAKILL's main thread ended up
// with pc=0x18000004 — which is SA_SIGINFO|SA_ONSTACK|SA_RESTART, the flags the guest
// had passed — spinning at 100% of one core inside _sigtramp: signal arrives, jump to
// 0x18000004, fault fetching the instruction, signal arrives, forever. It also
// silently uninstalled KuDroid's own crash handler, which is why that run produced no
// crash log.
//
// Why translating is not optional. Three things differ between a guest sigaction and
// a host one, and getting any of them wrong is fatal rather than degraded:
//
//   Struct layout — as above.
//
//   Signal numbers — Linux and Darwin disagree past SIGFPE. Guest SIGUSR1 is 10,
//   which on Darwin is SIGBUS; guest SIGSYS is 31, which on Darwin is SIGUSR2. A
//   guest installing a plain SIGUSR1 handler would take over KuDroid's SIGBUS
//   handling. The table here is built from the host's own SIG* macros so it is
//   correct by construction on either platform rather than by a copied number.
//
//   Ownership — KuDroid needs some signals for itself, and not merely for
//   diagnostics. Guest `mrs xN, tpidr_el0` is rewritten to `BRK #(0x1000+N)` at load
//   time and SIGTRAP is what supplies the TLS pointer; a raw guest `svc #0` arrives
//   as SIGSYS and is emulated there. If the guest replaces those handlers the guest
//   stops working immediately. So a handler for an owned signal is RECORDED and
//   dispatched to from KuDroid's handler, never installed over it.
#ifndef KUDROID_ABI_GUESTSIGNALS_H
#define KUDROID_ABI_GUESTSIGNALS_H

#include <cstdint>

namespace kudroid {

// `struct sigaction` as a 64-bit Android guest sees it.
//
// Copied field for field from bionic's __SIGACTION_BODY under `#if defined(__LP64__)`
// (libc/include/bits/signal_types.h). The layout follows from the declaration under
// LP64 rules, so it is right on any host with the same ABI — no offsets are hard-coded
// here beyond the static_asserts that pin them.
struct GuestSigaction {
    int32_t sa_flags;
    // The union bionic declares. Kept as a raw pointer because this side never calls
    // it directly: dispatch goes through guest_signal_dispatch, which builds the
    // arguments the guest expects.
    void* sa_handler_or_sigaction;
    // sigset_t on LP64 bionic is a bare unsigned long, and it is a Linux signal
    // bitmask: bit (n-1) is Linux signal n, not the host's n.
    uint64_t sa_mask;
    void (*sa_restorer)(void);
};

// The number of signals a Linux guest can name (bionic's NSIG is 65).
constexpr int kGuestNSIG = 65;

// Translate a signal number between the guest's Linux numbering and the host's.
// Returns 0 for a number with no counterpart, which callers must treat as EINVAL
// rather than passing on.
int guest_signal_to_host(int guest_signum);
int host_signal_to_guest(int host_signum);

// The guest's sigaction, with everything above accounted for. Semantics follow the
// Linux syscall: 0 on success, -1 with errno set on failure, and `oldact` filled in
// with what was previously installed when it is non-null.
//
// A handler for a signal KuDroid owns is accepted and recorded — refusing it would
// make a guest crash reporter fail to initialise — but KuDroid's own handler stays
// installed and calls the guest's from guest_signal_dispatch.
int guest_sigaction(int guest_signum, const GuestSigaction* act, GuestSigaction* oldact);

// Run the guest's handler for a signal that has just arrived on a host signal number.
//
// Called from KuDroid's crash handler for the signals it owns. Returns true when the
// guest handler ran AND changed the machine state — which is the guest saying it has
// dealt with the fault, the way a runtime that patches up a null dereference does.
// Returns false when there is no guest handler, or when the handler ran and left pc
// untouched: in that case the fault is genuinely unhandled and the caller must carry
// on to crash reporting. Returning true unconditionally would turn a real crash into
// the same infinite signal loop this file exists to fix.
bool guest_signal_dispatch(int host_signum, void* host_siginfo, void* host_ucontext);

// Whether the guest has a handler installed for a host signal number. Cheap; for a
// caller that wants to know before doing expensive work.
bool guest_signal_has_handler(int host_signum);

// The guest's sigaltstack. Separate from the host's because Linux's stack_t is
// {sp, flags, size} and Darwin's is {sp, size, flags} — read through the wrong one, a
// guest asking for a 64KB alternate stack passes ss_flags=65536, the host rejects it,
// and the guest carries on believing it has an alternate stack it does not have.
int guest_sigaltstack(const void* guest_ss, void* guest_oss);

// Test seam: drop every recorded guest handler. Not for use on a live guest.
void guest_signal_reset_for_test();

}  // namespace kudroid

#endif  // KUDROID_ABI_GUESTSIGNALS_H
