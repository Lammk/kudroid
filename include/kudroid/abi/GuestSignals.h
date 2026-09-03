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

// ── Sending a signal ─────────────────────────────────────────────────────────
//
// The other half of the translation, and it was missing entirely.
//
// sigaction translated the number on the way IN, so a guest handler for Linux SIGPWR
// (30) was installed on the host signal that carries it — SIGINFO (29) on Darwin. But
// raise, kill and pthread_kill were not shimmed at all, so the guest's own number went
// straight to the host: pthread_kill(tid, 30) delivered Darwin signal 30, which is
// SIGUSR1. The handler sat in slot 29 while the signal arrived at slot 30, and
// SIGUSR1's default action is to terminate the process.
//
// That is Mono's SIG_SUSPEND, which is how il2cpp stops threads for GC. The guest
// installs it successfully — the log confirms it — and then every attempt to use it is
// delivered somewhere else. Twelve of the thirty-one named signals are misdirected this
// way, and the direction of the error is not visible from either side on its own: the
// install succeeds and the send succeeds.
//
// All four follow the Linux syscall contract: 0, or -1 with errno set.
int guest_raise(int guest_signum);
int guest_kill(int pid, int guest_signum);
int guest_pthread_kill(unsigned long thread, int guest_signum);
int guest_tgkill(int tgid, int tid, int guest_signum);

// ── Blocking a signal ────────────────────────────────────────────────────────
//
// Two translations, not one. The signal numbers in the set, and `how` itself:
//
//     Linux : SIG_BLOCK=0  SIG_UNBLOCK=1  SIG_SETMASK=2
//     Darwin: SIG_BLOCK=1  SIG_UNBLOCK=2  SIG_SETMASK=3
//
// Forwarding `how` unchanged does not fail — it silently means something else. A guest
// blocking signals (Linux 0) passes 0, which is not any Darwin constant; a guest
// UNBLOCKING (Linux 1) passes 1, which Darwin reads as BLOCK. So a runtime that blocks
// its suspend signal around a critical section and unblocks it after ends up with the
// signal blocked permanently, and its GC hangs rather than crashing.
//
// `guest_set` and `guest_oldset` are Linux sigset_t: a bare 64-bit word where bit (n-1)
// is Linux signal n. Either may be null, as in the syscall.
int guest_sigprocmask(int guest_how, const uint64_t* guest_set, uint64_t* guest_oldset);

// sigsuspend and sigwait, for completeness: both take a Linux mask and both are
// reachable from a managed runtime's thread control.
int guest_sigsuspend(const uint64_t* guest_mask);
int guest_sigwait(const uint64_t* guest_set, int* guest_signum_out);

// The simple handler interface, which is still what a lot of guest code uses. Returns
// the previous handler, or SIG_ERR. Recorded through the same registry as sigaction so
// the two cannot disagree about what is installed.
void* guest_signal(int guest_signum, void* guest_handler);

// Test seam: drop every recorded guest handler. Not for use on a live guest.
void guest_signal_reset_for_test();

}  // namespace kudroid

#endif  // KUDROID_ABI_GUESTSIGNALS_H
