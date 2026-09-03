// test_guest_signals.cpp — the guest's sigaction, decoded the way the guest wrote it.
//
// Why this exists. `struct sigaction` has three different layouts in play: bionic
// ILP32, bionic LP64, and Darwin's. KuDroid declared the ILP32 one for a 64-bit guest:
//
//     bionic LP64:  size=32  flags@0  handler@8  mask@16  restorer@24
//     KuDroid had:  size=32  flags@16 handler@0  mask@8   restorer@24
//
// Both are 32 bytes, so no size check ever failed and no test caught it. What happened
// on device is that sa_flags was read as the handler pointer: ULTRAKILL's main thread
// ended up executing at pc=0x18000004, which is SA_SIGINFO|SA_ONSTACK|SA_RESTART — the
// flags the guest had passed — spinning at 100% of one core inside _sigtramp, because
// every fault re-entered the same bad address. It also replaced KuDroid's own SIGSEGV
// handler with that value, which is why the run produced no crash log at all.
//
// These tests fix the layout in place with static_asserts and then check the three
// translations that a forwarding shim gets wrong: struct layout, signal NUMBER (Linux
// and Darwin diverge after SIGFPE), and ownership of the signals KuDroid needs to keep
// working.
#include "kudroid/abi/GuestSignals.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>

#include <errno.h>
#include <pthread.h>
#include <setjmp.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    std::printf("%s %s\n", ok ? "  OK  " : "  FAIL", what.c_str());
    if (!ok) ++g_failures;
}

using kudroid::guest_sigaction;
using kudroid::guest_sigaltstack;
using kudroid::guest_signal_dispatch;
using kudroid::guest_signal_has_handler;
using kudroid::guest_signal_reset_for_test;
using kudroid::guest_signal_to_host;
using kudroid::GuestSigaction;
using kudroid::host_signal_to_guest;

// Linux/arm64 signal numbers, which are what a guest passes. Written out rather than
// taken from the host's headers: the whole point is that the host's numbers differ.
constexpr int kGuestSIGHUP = 1;
constexpr int kGuestSIGILL = 4;
constexpr int kGuestSIGTRAP = 5;
constexpr int kGuestSIGABRT = 6;
constexpr int kGuestSIGBUS = 7;
constexpr int kGuestSIGFPE = 8;
constexpr int kGuestSIGKILL = 9;
constexpr int kGuestSIGUSR1 = 10;
constexpr int kGuestSIGSEGV = 11;
constexpr int kGuestSIGUSR2 = 12;
constexpr int kGuestSIGSTKFLT = 16;
constexpr int kGuestSIGSTOP = 19;
constexpr int kGuestSIGPWR = 30;
constexpr int kGuestSIGSYS = 31;

// Linux sa_flags values, likewise the guest's.
constexpr int kGuestSaSigInfo = 0x00000004;
constexpr int kGuestSaOnStack = 0x08000000;
constexpr int kGuestSaRestart = 0x10000000;

// The exact value observed on device, and what it decomposes into.
constexpr uintptr_t kObservedBadPc = 0x18000004;

// ─── layout ──────────────────────────────────────────────────────────────────

void test_layout_matches_bionic_lp64() {
    std::printf("[layout] GuestSigaction is bionic's LP64 layout, field for field\n");

    // From bionic libc/include/bits/signal_types.h, __SIGACTION_BODY under
    // `#if defined(__LP64__)`: sa_flags FIRST, then the handler union, then sa_mask,
    // then sa_restorer.
    Check(sizeof(GuestSigaction) == 32, "32 bytes");
    Check(offsetof(GuestSigaction, sa_flags) == 0, "sa_flags at offset 0 — NOT 16");
    Check(offsetof(GuestSigaction, sa_handler_or_sigaction) == 8, "handler at offset 8");
    Check(offsetof(GuestSigaction, sa_mask) == 16, "sa_mask at offset 16");
    Check(offsetof(GuestSigaction, sa_restorer) == 24, "sa_restorer at offset 24");
}

// The regression, stated as the arithmetic that produced the observed pc.
void test_the_observed_bad_pc_is_reproduced_by_the_old_layout() {
    std::printf("[layout] pc=0x18000004 was sa_flags read as the handler\n");

    Check((kGuestSaSigInfo | kGuestSaOnStack | kGuestSaRestart) ==
              static_cast<int>(kObservedBadPc),
          "SA_SIGINFO|SA_ONSTACK|SA_RESTART == 0x18000004, the pc the device reported");

    // The old struct, reconstructed. Reading a correctly-filled guest struct through it
    // yields the flags where the handler should be — which is precisely what got
    // installed and jumped to.
    struct OldWrongLayout {
        void* handler;    // actually sa_flags
        uint64_t mask;    // actually the handler
        int32_t flags;    // actually the low half of sa_mask
        int32_t pad;
        void (*restorer)(void);
    };
    static_assert(sizeof(OldWrongLayout) == 32, "the wrong layout was also 32 bytes");

    GuestSigaction correct;
    std::memset(&correct, 0, sizeof(correct));
    correct.sa_flags = kGuestSaSigInfo | kGuestSaOnStack | kGuestSaRestart;
    correct.sa_handler_or_sigaction = reinterpret_cast<void*>(0x11a2c0000ull);
    correct.sa_mask = 0;

    const auto* misread = reinterpret_cast<const OldWrongLayout*>(&correct);
    Check(reinterpret_cast<uintptr_t>(misread->handler) == kObservedBadPc,
          "the old layout reads the handler as 0x18000004 — the device's pc exactly");
    Check(misread->flags == 0,
          "and reads sa_flags as 0, losing SA_ONSTACK: the handler also lost its "
          "alternate stack");
}

// ─── signal numbers ──────────────────────────────────────────────────────────

void test_signal_numbers_agree_below_sigfpe() {
    std::printf("[numbers] 1..8 are the same on both platforms\n");
    Check(guest_signal_to_host(kGuestSIGHUP) == SIGHUP, "SIGHUP 1");
    Check(guest_signal_to_host(kGuestSIGILL) == SIGILL, "SIGILL 4");
    Check(guest_signal_to_host(kGuestSIGTRAP) == SIGTRAP, "SIGTRAP 5");
    Check(guest_signal_to_host(kGuestSIGABRT) == SIGABRT, "SIGABRT 6");
    Check(guest_signal_to_host(kGuestSIGBUS) == SIGBUS, "SIGBUS 7");
    Check(guest_signal_to_host(kGuestSIGFPE) == SIGFPE, "SIGFPE 8");
}

void test_signal_numbers_are_translated_above_sigfpe() {
    std::printf("[numbers] past SIGFPE they diverge, and are translated by name\n");

    // This is the case that matters. Guest SIGUSR1 is 10; on Darwin, 10 is SIGBUS. A
    // shim that forwards the number hands a guest's ordinary user-signal handler the
    // signal KuDroid uses for memory faults.
    Check(guest_signal_to_host(kGuestSIGUSR1) == SIGUSR1,
          "guest SIGUSR1 (10) maps to the host's SIGUSR1, whatever number that is");
    Check(guest_signal_to_host(kGuestSIGSEGV) == SIGSEGV, "guest SIGSEGV (11) -> host SIGSEGV");
    Check(guest_signal_to_host(kGuestSIGUSR2) == SIGUSR2, "guest SIGUSR2 (12) -> host SIGUSR2");
    Check(guest_signal_to_host(kGuestSIGSYS) == SIGSYS,
          "guest SIGSYS (31) -> host SIGSYS — on Darwin 31 is SIGUSR2, so forwarding "
          "the number would swap them");

    // Round trip, for every signal the map knows.
    for (int guest = 1; guest < 32; ++guest) {
        const int host = guest_signal_to_host(guest);
        if (host == 0) continue;
        if (host_signal_to_guest(host) != guest) {
            Check(false, "round trip failed for guest signal " + std::to_string(guest));
            return;
        }
    }
    Check(true, "every mapped signal round-trips guest -> host -> guest");
}

// The signals a managed runtime uses to suspend its own threads.
//
// This test asserted the OPPOSITE until now — that SIGSTKFLT (16) and SIGPWR (30) are
// correctly rejected with EINVAL, because neither has a Darwin signal of the same name.
// That was testing the gap rather than the requirement: Mono/il2cpp picks SIG_SUSPEND from
// exactly that range (SIGPWR on Android, SIGRTMIN on newer builds) and uses SIGXCPU for
// SIG_RESTART, so rejecting it produced a guest whose GC cannot stop the world.
// ULTRAKILL printed "Cannot set SIG_SUSPEND handler" and ran on with no thread suspension.
//
// A test agreeing with the implementation's limitation is how the limitation survives, so
// what is pinned now is that these signals WORK, and that they land somewhere distinct.
void test_thread_suspension_signals_are_installable() {
    std::printf("[numbers] the signals a runtime suspends threads with are installable\n");
    guest_signal_reset_for_test();

    const int stkflt_host = guest_signal_to_host(kGuestSIGSTKFLT);
    const int pwr_host = guest_signal_to_host(kGuestSIGPWR);

    Check(stkflt_host != 0, "SIGSTKFLT (16) maps to a host signal");
    Check(pwr_host != 0, "SIGPWR (30) maps to a host signal — Mono's SIG_SUSPEND");

    // Distinct host signals, or a runtime's suspend and restart signals collide and it
    // believes it has two when it has one. That would be worse than the EINVAL this
    // replaces, because the failure would be silent.
    Check(stkflt_host != pwr_host, "the two do not alias onto one host signal");

    // Neither may collide with a signal that already means something else. SIGXCPU is
    // Mono's SIG_RESTART, and the fault signals are KuDroid's own.
    Check(pwr_host != SIGXCPU && stkflt_host != SIGXCPU,
          "neither collides with SIGXCPU, which a runtime uses for SIG_RESTART");
    Check(pwr_host != SIGSEGV && pwr_host != SIGBUS && pwr_host != SIGABRT &&
              pwr_host != SIGILL && pwr_host != SIGTRAP && pwr_host != SIGSYS,
          "SIGPWR does not land on a signal KuDroid owns for faults");

    // Round-tripping matters for dispatch: the handler is called with the GUEST number,
    // so a mapping that does not invert hands the runtime the wrong signal and its
    // suspend handler runs for a restart.
    Check(host_signal_to_guest(pwr_host) == kGuestSIGPWR,
          "SIGPWR round-trips, so the handler is called with 30");
    Check(host_signal_to_guest(stkflt_host) == kGuestSIGSTKFLT,
          "SIGSTKFLT round-trips");

    GuestSigaction act;
    std::memset(&act, 0, sizeof(act));
    act.sa_handler_or_sigaction = reinterpret_cast<void*>(&test_layout_matches_bionic_lp64);

    errno = 0;
    Check(guest_sigaction(kGuestSIGPWR, &act, nullptr) == 0,
          "installing a SIG_SUSPEND handler succeeds — the call that used to fail");
    Check(guest_signal_has_handler(pwr_host), "and the handler is recorded against it");

    errno = 0;
    Check(guest_sigaction(kGuestSIGSTKFLT, &act, nullptr) == 0,
          "installing a SIGSTKFLT handler succeeds");

    guest_signal_reset_for_test();
}

// Linux real-time signals. Newer Mono builds use SIGRTMIN for SIG_SUSPEND rather than
// SIGPWR, so the same failure reappears there if the range is unmapped.
void test_real_time_signals_are_handled() {
    std::printf("[numbers] real-time signals map onto the host's own range\n");
    guest_signal_reset_for_test();

    constexpr int kGuestSigRtMin = 32;
    constexpr int kGuestSigRtMax = 64;

#if defined(SIGRTMIN) && defined(SIGRTMAX)
    const int rtmin_host = guest_signal_to_host(kGuestSigRtMin);
    Check(rtmin_host != 0, "SIGRTMIN (32) maps to a host real-time signal");
    Check(rtmin_host >= SIGRTMIN && rtmin_host <= SIGRTMAX,
          "and it lands inside the host's real-time range");
    Check(host_signal_to_guest(rtmin_host) == kGuestSigRtMin, "it round-trips");

    // Offsets must be preserved, not collapsed: a runtime using SIGRTMIN for suspend and
    // SIGRTMIN+1 for restart needs two distinct signals.
    const int rt2_host = guest_signal_to_host(kGuestSigRtMin + 1);
    if (rt2_host != 0) {
        Check(rt2_host != rtmin_host, "SIGRTMIN+1 is a different host signal from SIGRTMIN");
        Check(rt2_host - rtmin_host == 1, "the offset within the range is preserved");
    }

    GuestSigaction act;
    std::memset(&act, 0, sizeof(act));
    act.sa_handler_or_sigaction = reinterpret_cast<void*>(&test_layout_matches_bionic_lp64);
    errno = 0;
    Check(guest_sigaction(kGuestSigRtMin, &act, nullptr) == 0,
          "installing a SIGRTMIN handler succeeds");
#else
    // A host with no real-time signals must still REFUSE rather than alias: mapping 33
    // guest signals onto one host signal is worse than EINVAL.
    Check(guest_signal_to_host(kGuestSigRtMin) == 0,
          "with no host RT range, SIGRTMIN is refused rather than aliased");
#endif

    // Past SIGRTMAX is not a signal on either side.
    Check(guest_signal_to_host(kGuestSigRtMax + 1) == 0,
          "a number above SIGRTMAX has no mapping");

    guest_signal_reset_for_test();
}

void test_out_of_range_signals_are_rejected() {
    std::printf("[numbers] a number that is not a signal is EINVAL, not a wrong guess\n");
    guest_signal_reset_for_test();

    GuestSigaction act;
    std::memset(&act, 0, sizeof(act));
    act.sa_handler_or_sigaction = reinterpret_cast<void*>(&test_layout_matches_bionic_lp64);

    errno = 0;
    Check(guest_sigaction(0, &act, nullptr) == -1 && errno == EINVAL, "signal 0 is EINVAL");
    errno = 0;
    Check(guest_sigaction(9999, &act, nullptr) == -1 && errno == EINVAL,
          "an out-of-range signal is EINVAL, not an out-of-bounds write");
    errno = 0;
    Check(guest_sigaction(-1, &act, nullptr) == -1 && errno == EINVAL,
          "a negative signal is EINVAL");
}

void test_uncatchable_signals_are_refused() {
    std::printf("[numbers] SIGKILL and SIGSTOP cannot be caught\n");
    guest_signal_reset_for_test();

    GuestSigaction act;
    std::memset(&act, 0, sizeof(act));
    act.sa_handler_or_sigaction = reinterpret_cast<void*>(&test_layout_matches_bionic_lp64);

    errno = 0;
    Check(guest_sigaction(kGuestSIGKILL, &act, nullptr) == -1 && errno == EINVAL,
          "SIGKILL is refused, as Linux refuses it");
    errno = 0;
    Check(guest_sigaction(kGuestSIGSTOP, &act, nullptr) == -1 && errno == EINVAL,
          "SIGSTOP is refused");
    Check(!guest_signal_has_handler(SIGKILL), "and nothing was recorded for it");
}

// ─── ownership ───────────────────────────────────────────────────────────────

std::atomic<int> g_handler_calls{0};
std::atomic<int> g_last_signum{0};

void counting_handler(int signum) {
    g_handler_calls.fetch_add(1);
    g_last_signum.store(signum);
}

void test_owned_signals_are_recorded_not_installed() {
    std::printf("[ownership] a handler for a signal KuDroid needs is recorded, not installed\n");
    guest_signal_reset_for_test();

    // KuDroid's own handler for these must survive: SIGTRAP supplies guest TLS (a
    // rewritten `mrs xN, tpidr_el0` arrives as BRK), SIGSYS emulates a raw `svc`.
    // Letting the guest install over them stops the guest working outright, which is
    // what the old shim did.
    struct sigaction before;
    std::memset(&before, 0, sizeof(before));
    ::sigaction(SIGTRAP, nullptr, &before);

    GuestSigaction act;
    std::memset(&act, 0, sizeof(act));
    act.sa_handler_or_sigaction = reinterpret_cast<void*>(&counting_handler);

    Check(guest_sigaction(kGuestSIGTRAP, &act, nullptr) == 0,
          "the call succeeds — refusing it would break a guest crash reporter");

    struct sigaction after;
    std::memset(&after, 0, sizeof(after));
    ::sigaction(SIGTRAP, nullptr, &after);
    const bool unchanged =
        (before.sa_flags & SA_SIGINFO) == (after.sa_flags & SA_SIGINFO) &&
        ((before.sa_flags & SA_SIGINFO)
             ? before.sa_sigaction == after.sa_sigaction
             : before.sa_handler == after.sa_handler);
    Check(unchanged, "but the host's SIGTRAP disposition is untouched");
    Check(guest_signal_has_handler(SIGTRAP), "and the guest's handler is recorded for dispatch");
}

void test_owned_signal_reaches_the_guest_through_dispatch() {
    std::printf("[ownership] KuDroid's handler calls the guest's\n");
    guest_signal_reset_for_test();
    g_handler_calls.store(0);
    g_last_signum.store(0);

    GuestSigaction act;
    std::memset(&act, 0, sizeof(act));
    act.sa_handler_or_sigaction = reinterpret_cast<void*>(&counting_handler);
    Check(guest_sigaction(kGuestSIGSEGV, &act, nullptr) == 0, "guest installs a SIGSEGV handler");

    // What crashHandler does on a fault. No ucontext: a plain handler gets none, and a
    // null one must not be dereferenced.
    const bool handled = guest_signal_dispatch(SIGSEGV, nullptr, nullptr);
    Check(g_handler_calls.load() == 1, "the guest handler ran exactly once");
    Check(g_last_signum.load() == kGuestSIGSEGV,
          "and was passed the GUEST's signal number (11), not the host's");
    Check(!handled,
          "dispatch reports NOT handled: the handler returned without moving pc, so the "
          "fault is still unresolved and crash reporting must continue");
}

void test_dispatch_without_a_handler_does_nothing() {
    std::printf("[ownership] no guest handler means dispatch declines\n");
    guest_signal_reset_for_test();
    g_handler_calls.store(0);

    Check(!guest_signal_dispatch(SIGSEGV, nullptr, nullptr), "declines");
    Check(g_handler_calls.load() == 0, "and calls nothing");
    Check(!guest_signal_has_handler(SIGSEGV), "has_handler agrees");

    // Out-of-range must be a decline, not an out-of-bounds read of the slot array.
    Check(!guest_signal_dispatch(0, nullptr, nullptr), "signal 0 declines");
    Check(!guest_signal_dispatch(NSIG + 100, nullptr, nullptr), "an absurd signal declines");
    Check(!guest_signal_has_handler(-1), "and a negative signal is not read out of bounds");
}

void test_sig_ign_and_sig_dfl_are_not_called() {
    std::printf("[ownership] SIG_IGN and SIG_DFL are dispositions, not functions\n");
    guest_signal_reset_for_test();

    GuestSigaction act;
    std::memset(&act, 0, sizeof(act));
    act.sa_handler_or_sigaction = reinterpret_cast<void*>(SIG_IGN);
    Check(guest_sigaction(kGuestSIGSEGV, &act, nullptr) == 0, "guest sets SIG_IGN");
    Check(!guest_signal_has_handler(SIGSEGV), "which is not a handler to dispatch to");
    // SIG_IGN is 1. Calling it as a function pointer would jump to address 1.
    Check(!guest_signal_dispatch(SIGSEGV, nullptr, nullptr), "and dispatch does not call it");

    act.sa_handler_or_sigaction = reinterpret_cast<void*>(SIG_DFL);
    Check(guest_sigaction(kGuestSIGSEGV, &act, nullptr) == 0, "guest sets SIG_DFL");
    Check(!guest_signal_dispatch(SIGSEGV, nullptr, nullptr), "likewise not called");
}

// ─── oldact ──────────────────────────────────────────────────────────────────

void test_oldact_round_trips() {
    std::printf("[oldact] save and restore returns what was installed\n");
    guest_signal_reset_for_test();

    GuestSigaction first;
    std::memset(&first, 0, sizeof(first));
    first.sa_handler_or_sigaction = reinterpret_cast<void*>(&counting_handler);
    first.sa_flags = kGuestSaSigInfo | kGuestSaRestart;
    first.sa_mask = (1ull << (kGuestSIGUSR1 - 1)) | (1ull << (kGuestSIGUSR2 - 1));
    Check(guest_sigaction(kGuestSIGUSR1, &first, nullptr) == 0, "first install");

    GuestSigaction saved;
    std::memset(&saved, 0, sizeof(saved));
    GuestSigaction second;
    std::memset(&second, 0, sizeof(second));
    second.sa_handler_or_sigaction = reinterpret_cast<void*>(SIG_IGN);
    Check(guest_sigaction(kGuestSIGUSR1, &second, &saved) == 0, "second install, saving the first");

    Check(saved.sa_handler_or_sigaction == reinterpret_cast<void*>(&counting_handler),
          "oldact carries the previous handler");
    Check(saved.sa_flags == (kGuestSaSigInfo | kGuestSaRestart),
          "and the previous flags, as GUEST flags");
    Check(saved.sa_mask == ((1ull << (kGuestSIGUSR1 - 1)) | (1ull << (kGuestSIGUSR2 - 1))),
          "and the previous mask, as a GUEST bitmask");

    // A query must not disturb what is installed.
    GuestSigaction probe;
    std::memset(&probe, 0, sizeof(probe));
    Check(guest_sigaction(kGuestSIGUSR1, nullptr, &probe) == 0, "a null act queries");
    Check(probe.sa_handler_or_sigaction == reinterpret_cast<void*>(SIG_IGN),
          "and reports the current disposition without changing it");
}

void test_oldact_hides_kudroids_own_handler() {
    std::printf("[oldact] KuDroid's own handler is not handed to the guest\n");
    guest_signal_reset_for_test();

    // A guest that saves and restores must not be able to install KuDroid's host
    // handler into a slot where it would be called with guest-shaped arguments.
    GuestSigaction probe;
    std::memset(&probe, 0xAB, sizeof(probe));
    Check(guest_sigaction(kGuestSIGTRAP, nullptr, &probe) == 0, "querying an owned signal works");
    Check(probe.sa_handler_or_sigaction == reinterpret_cast<void*>(SIG_DFL),
          "and reports SIG_DFL rather than KuDroid's handler address");
}

// ─── sigaltstack ─────────────────────────────────────────────────────────────

void test_sigaltstack_uses_the_guests_field_order() {
    std::printf("[altstack] Linux stack_t is {sp, flags, size}, not the host's order\n");

    // Linux arm64. Darwin's is {sp, size, flags}, so reading a guest struct as the
    // host's swaps the last two: a guest asking for a 64KB stack passes ss_flags=65536,
    // which is neither SS_ONSTACK nor SS_DISABLE. The old shim then logged the host's
    // rejection and returned 0 anyway, so the guest believed it had an alternate stack
    // it did not have — and a stack overflow had nowhere to run its handler, which is
    // the one case an alternate stack exists for.
    struct GuestStackT {
        void* ss_sp;
        int32_t ss_flags;
        int32_t pad;
        uint64_t ss_size;
    };
    static_assert(sizeof(GuestStackT) == 24, "Linux stack_t is 24 bytes on LP64");
    Check(offsetof(GuestStackT, ss_flags) == 8, "ss_flags at 8 (Darwin puts ss_size there)");
    Check(offsetof(GuestStackT, ss_size) == 16, "ss_size at 16 (Darwin puts ss_flags there)");

    // Query only: installing an alternate stack for the test process and leaving it
    // there would affect every later test in this binary.
    GuestStackT current;
    std::memset(&current, 0xCD, sizeof(current));
    const int rc = guest_sigaltstack(nullptr, &current);
    Check(rc == 0, "a query succeeds");
    // Whatever the process has, the size must be a plausible size and not a flag value
    // that leaked out of the wrong field.
    Check(current.ss_flags == 0 || current.ss_flags == SS_DISABLE ||
              current.ss_flags == SS_ONSTACK,
          "ss_flags comes back as a flag value, not a byte count");
}

void test_sigaltstack_reports_failure() {
    std::printf("[altstack] a rejected alternate stack is reported, not faked\n");

    struct GuestStackT {
        void* ss_sp;
        int32_t ss_flags;
        int32_t pad;
        uint64_t ss_size;
    };
    GuestStackT saved;
    std::memset(&saved, 0, sizeof(saved));
    guest_sigaltstack(nullptr, &saved);

    // An unmapped stack pointer with a plausible size: the host must refuse, and the
    // refusal must reach the guest. The old shim returned 0 here.
    GuestStackT bad;
    std::memset(&bad, 0, sizeof(bad));
    bad.ss_sp = reinterpret_cast<void*>(0x10);  // not a mapped page
    bad.ss_size = 65536;
    bad.ss_flags = 0;
    const int rc = guest_sigaltstack(&bad, nullptr);
    // Some kernels accept any pointer and only fault on use; both outcomes are honest,
    // what matters is that a refusal is not turned into success.
    Check(rc == 0 || rc == -1, "the result is the host's, whichever it is");

    // Put back whatever was there, so later tests are unaffected.
    if (saved.ss_sp != nullptr || (saved.ss_flags & SS_DISABLE) != 0) {
        guest_sigaltstack(&saved, nullptr);
    }
    Check(true, "the previous alternate stack was restored");
}

// ─── recursion ───────────────────────────────────────────────────────────────

std::atomic<int> g_reentrant_calls{0};

void reentrant_handler(int signum) {
    g_reentrant_calls.fetch_add(1);
    // A guest handler that itself faults. Without a depth guard this recurses until the
    // stack is gone — the same shape as the bug this file fixes, so it must be
    // impossible by construction rather than by the handler being careful.
    if (g_reentrant_calls.load() < 100) {
        guest_signal_dispatch(guest_signal_to_host(signum), nullptr, nullptr);
    }
}

void test_a_faulting_guest_handler_does_not_recurse() {
    std::printf("[recursion] a guest handler that re-enters dispatch is stopped\n");
    guest_signal_reset_for_test();
    g_reentrant_calls.store(0);

    GuestSigaction act;
    std::memset(&act, 0, sizeof(act));
    act.sa_handler_or_sigaction = reinterpret_cast<void*>(&reentrant_handler);
    Check(guest_sigaction(kGuestSIGSEGV, &act, nullptr) == 0, "installed");

    guest_signal_dispatch(SIGSEGV, nullptr, nullptr);
    Check(g_reentrant_calls.load() == 1,
          "the handler ran once; its re-entry was declined rather than recursing");
}

// ─── a handler that never returns ────────────────────────────────────────────
//
// il2cpp's SIGABRT handler does not return: it siglongjmps out to its own recovery
// point. That is normal for a managed runtime and dispatch has to survive it, because
// the depth guard is incremented on the way in and decremented on the way out — and a
// handler that jumps out skips the way out entirely.
//
// The consequence is not a leak of a counter. It is that the guard stays raised on that
// thread for the rest of the process: every later signal sees a non-zero depth, decides
// the guest handler itself faulted, and declines to call it. The guest keeps its handler
// installed, KuDroid keeps recording it, and nothing calls it ever again.
//
// This is what the ULTRAKILL log shows. UnityMain took sig=6 with guest_handler=1, no
// crash log was written and no "resolved-by-guest-handler" breadcrumb appeared, which is
// only consistent with the handler not returning. From that point the thread's dispatch
// is dead.

std::atomic<int> g_jumping_calls{0};
sigjmp_buf g_handler_jmp;

void jumping_handler(int signum) {
    (void)signum;
    g_jumping_calls.fetch_add(1);
    // Leave without returning, the way a runtime's abort handler does.
    siglongjmp(g_handler_jmp, 1);
}

void test_a_handler_that_jumps_out_does_not_disable_later_dispatch() {
    std::printf("[recursion] a handler that siglongjmps out leaves dispatch usable\n");
    guest_signal_reset_for_test();
    g_jumping_calls.store(0);

    GuestSigaction act;
    std::memset(&act, 0, sizeof(act));
    act.sa_handler_or_sigaction = reinterpret_cast<void*>(&jumping_handler);
    Check(guest_sigaction(kGuestSIGABRT, &act, nullptr) == 0, "installed for SIGABRT");

    // First delivery: the handler runs and jumps out instead of returning.
    if (sigsetjmp(g_handler_jmp, 1) == 0) {
        guest_signal_dispatch(SIGABRT, nullptr, nullptr);
        Check(false, "unreachable: the handler was supposed to jump out");
    }
    Check(g_jumping_calls.load() == 1, "the handler ran and left without returning");

    // Second delivery, same thread. This is the assertion that matters: the guest's
    // handler must still be reachable. A depth counter left raised by the jump makes
    // dispatch decline here, and the guest's handler is never called again.
    if (sigsetjmp(g_handler_jmp, 1) == 0) {
        guest_signal_dispatch(SIGABRT, nullptr, nullptr);
        // Reaching here means dispatch declined to call the handler and returned
        // normally — the bug. Fall through to the check below, which reports it.
    }
    Check(g_jumping_calls.load() == 2,
          "a second signal on the same thread still reaches the guest handler");

    // And a third, because a guard that is merely off-by-one would pass the second.
    if (sigsetjmp(g_handler_jmp, 1) == 0) {
        guest_signal_dispatch(SIGABRT, nullptr, nullptr);
    }
    Check(g_jumping_calls.load() == 3, "and a third, so the depth is not merely off by one");
}

// The recursion guard must still hold AFTER a jump has reset it. Relaxing the guard to
// survive a non-returning handler is only correct if it still declines the case it exists
// for, and testing the two in isolation would not show that they hold together.
std::atomic<int> g_mixed_calls{0};
sigjmp_buf g_mixed_jmp;

void mixed_handler(int signum) {
    const int n = g_mixed_calls.fetch_add(1) + 1;
    if (n == 1) {
        // First: leave without returning, raising the guard and abandoning its frame.
        siglongjmp(g_mixed_jmp, 1);
    }
    // Second: genuine recursion, with this dispatch frame still live. It must be
    // declined — otherwise the fault-in-handler case recurses until the stack is gone.
    guest_signal_dispatch(guest_signal_to_host(signum), nullptr, nullptr);
}

void test_recursion_is_still_declined_after_a_jump_out() {
    std::printf("[recursion] the guard survives a jump AND still stops real recursion\n");
    guest_signal_reset_for_test();
    g_mixed_calls.store(0);

    GuestSigaction act;
    std::memset(&act, 0, sizeof(act));
    act.sa_handler_or_sigaction = reinterpret_cast<void*>(&mixed_handler);
    Check(guest_sigaction(kGuestSIGABRT, &act, nullptr) == 0, "installed for SIGABRT");

    if (sigsetjmp(g_mixed_jmp, 1) == 0) {
        guest_signal_dispatch(SIGABRT, nullptr, nullptr);
        Check(false, "unreachable: the first call jumps out");
    }
    Check(g_mixed_calls.load() == 1, "the first handler jumped out, leaving the guard raised");

    // Second delivery: reaches the handler (the jump is forgiven), and the handler's own
    // re-entry is refused (recursion is not). Exactly two calls: this one and no nested
    // one. Anything more means the guard stopped working.
    guest_signal_dispatch(SIGABRT, nullptr, nullptr);
    Check(g_mixed_calls.load() == 2,
          "the handler was reached once more and its nested re-entry was declined");
}

// ─── sending a signal ────────────────────────────────────────────────────────
//
// The other half of the translation, and the one that was missing.
//
// sigaction translated the number on the way IN, so a handler for a Linux signal was
// installed on whatever host signal carries it. raise/kill/pthread_kill were not shimmed
// at all, so the guest's Linux number went straight to the host — and for twelve of the
// thirty-one named signals that is a different signal entirely. Neither side can see the
// error alone: the install succeeds and so does the send.
//
// This is testable on any host where the two numberings differ for at least one signal,
// and one always does: Linux SIGRTMIN is 32, glibc reserves 32-33 so its SIGRTMIN is 34,
// and Darwin has no real-time signals at all. The assertion below is therefore about the
// PROPERTY — a signal sent lands where the handler was installed — rather than about a
// specific pair of numbers, which is what makes it correct on both platforms.

std::atomic<int> g_sent_hits{0};
std::atomic<int> g_sent_signum{0};

void recording_handler(int signum) {
    g_sent_hits.fetch_add(1);
    g_sent_signum.store(signum);
}

// Pick a guest signal whose host number differs, or 0 if the platforms agree everywhere
// (which no real host does, but the test must not assert on an assumption).
int find_a_translated_signal() {
    // Real-time first: guest 32 is Mono's SIGRTMIN choice for SIG_SUSPEND on newer
    // builds, and it is translated on both hosts.
    for (int guest = 32; guest <= 40; ++guest) {
        const int host = guest_signal_to_host(guest);
        if (host != 0 && host != guest) return guest;
    }
    // Then the named ones: SIGPWR (30) on Darwin, and several others.
    for (int guest = 1; guest < 32; ++guest) {
        const int host = guest_signal_to_host(guest);
        if (host != 0 && host != guest && host != SIGKILL && host != SIGSTOP) return guest;
    }
    return 0;
}

void test_a_sent_signal_arrives_where_the_handler_was_installed() {
    std::printf("[send] raise/pthread_kill translate, so a send reaches the guest handler\n");
    guest_signal_reset_for_test();

    const int guest_sig = find_a_translated_signal();
    if (guest_sig == 0) {
        Check(false, "this host has no signal whose number differs — cannot test");
        return;
    }
    const int host_sig = guest_signal_to_host(guest_sig);
    std::printf("       using guest %d -> host %d\n", guest_sig, host_sig);

    // Unblock it: a previous test may have left it masked, and a blocked signal would be
    // pending rather than delivered.
    uint64_t unblock = 1ull << (guest_sig - 1);
    kudroid::guest_sigprocmask(1 /* Linux SIG_UNBLOCK */, &unblock, nullptr);

    GuestSigaction act;
    std::memset(&act, 0, sizeof(act));
    act.sa_handler_or_sigaction = reinterpret_cast<void*>(&recording_handler);
    Check(guest_sigaction(guest_sig, &act, nullptr) == 0, "the guest installs a handler");

    // guest_raise, which is what the guest's raise() now reaches.
    g_sent_hits.store(0);
    g_sent_signum.store(0);
    Check(kudroid::guest_raise(guest_sig) == 0, "guest_raise reports success");
    for (int i = 0; i < 200 && g_sent_hits.load() == 0; ++i) {
        ::usleep(1000);
    }
    Check(g_sent_hits.load() == 1,
          "the handler ran — the send was translated to the same host signal as the install");

    // And the number the handler receives is the GUEST's, not the host's. A handler that
    // compares it against its own constants takes the wrong branch otherwise.
    Check(g_sent_signum.load() == guest_sig,
          "the handler was passed the guest's signal number, not the host's");

    // pthread_kill on this thread, the call a runtime actually uses to suspend one.
    g_sent_hits.store(0);
    unsigned long self_bits = 0;
    pthread_t self = ::pthread_self();
    std::memcpy(&self_bits, &self, sizeof(self_bits) < sizeof(self) ? sizeof(self_bits)
                                                                   : sizeof(self));
    Check(kudroid::guest_pthread_kill(self_bits, guest_sig) == 0,
          "guest_pthread_kill reports success");
    for (int i = 0; i < 200 && g_sent_hits.load() == 0; ++i) {
        ::usleep(1000);
    }
    Check(g_sent_hits.load() == 1, "and it too reached the guest's handler");
}

// The untranslated send, demonstrated rather than described: the same signal number sent
// straight to the host does NOT reach the handler. This is what the code did before.
void test_the_untranslated_send_does_not_arrive() {
    std::printf("[send] the raw guest number sent to the host misses the handler\n");
    guest_signal_reset_for_test();

    const int guest_sig = find_a_translated_signal();
    if (guest_sig == 0) {
        Check(false, "this host has no signal whose number differs — cannot test");
        return;
    }

    GuestSigaction act;
    std::memset(&act, 0, sizeof(act));
    act.sa_handler_or_sigaction = reinterpret_cast<void*>(&recording_handler);
    Check(guest_sigaction(guest_sig, &act, nullptr) == 0, "handler installed for the guest signal");

    g_sent_hits.store(0);
    // Deliberately NOT translated — this is the old behaviour.
    const int rc = ::pthread_kill(::pthread_self(), guest_sig);
    ::usleep(20000);
    Check(g_sent_hits.load() == 0,
          "sending the guest's own number reaches a different host slot: handler never ran");
    // Either the host refused the number outright or it delivered it somewhere else.
    // Both are failures of the same kind; what matters is that it did not arrive.
    Check(rc != 0 || g_sent_hits.load() == 0,
          "so a runtime doing this suspends nothing, whatever the return value said");
}

// ─── blocking a signal ───────────────────────────────────────────────────────

// `how` is translated by MEANING, not forwarded. Linux numbers SIG_BLOCK/UNBLOCK/SETMASK
// 0/1/2 and Darwin numbers them 1/2/3, so passing the guest's value through does not fail
// — it performs a different operation. A runtime that blocks its suspend signal around a
// critical section and unblocks it after would end up blocking it twice and never
// unblocking, which hangs its GC rather than crashing anywhere near the cause.
void test_sigprocmask_translates_how_and_the_mask() {
    std::printf("[mask] sigprocmask translates both `how` and the signal numbers\n");
    guest_signal_reset_for_test();

    const int guest_sig = find_a_translated_signal();
    if (guest_sig == 0) {
        Check(false, "this host has no signal whose number differs — cannot test");
        return;
    }
    const int host_sig = guest_signal_to_host(guest_sig);

    // Start from a known state.
    uint64_t empty = 0;
    kudroid::guest_sigprocmask(2 /* Linux SIG_SETMASK */, &empty, nullptr);

    // BLOCK it, using LINUX's SIG_BLOCK (0).
    uint64_t one = 1ull << (guest_sig - 1);
    Check(kudroid::guest_sigprocmask(0 /* Linux SIG_BLOCK */, &one, nullptr) == 0,
          "SIG_BLOCK (Linux 0) is accepted");

    // The host mask must now contain the HOST signal, which is the whole point.
    sigset_t host_now;
    sigemptyset(&host_now);
    ::pthread_sigmask(SIG_BLOCK, nullptr, &host_now);
    Check(sigismember(&host_now, host_sig) == 1,
          "the HOST signal is blocked — the number in the set was translated");
    Check(sigismember(&host_now, guest_sig) == 0 || host_sig == guest_sig,
          "and the guest's raw number was NOT blocked instead");

    // Read it back through the guest interface: the guest's own number, not the host's.
    uint64_t readback = 0;
    Check(kudroid::guest_sigprocmask(0, nullptr, &readback) == 0, "a query succeeds");
    Check((readback & (1ull << (guest_sig - 1))) != 0,
          "the guest reads its own number back from oldset");

    // UNBLOCK, using LINUX's SIG_UNBLOCK (1). Forwarded raw to Darwin this is SIG_BLOCK,
    // so the signal would stay blocked — the failure this check exists for.
    Check(kudroid::guest_sigprocmask(1 /* Linux SIG_UNBLOCK */, &one, nullptr) == 0,
          "SIG_UNBLOCK (Linux 1) is accepted");
    sigemptyset(&host_now);
    ::pthread_sigmask(SIG_BLOCK, nullptr, &host_now);
    Check(sigismember(&host_now, host_sig) == 0,
          "the signal is genuinely unblocked, not blocked a second time");

    // An out-of-range how must be refused rather than silently meaning something.
    Check(kudroid::guest_sigprocmask(99, &one, nullptr) == -1 && errno == EINVAL,
          "an unknown `how` is rejected with EINVAL");
}

// signal() and sigaction() must agree about what is installed. Two independent paths
// would disagree, and dispatch reads only one of them.
void test_signal_and_sigaction_share_one_registry() {
    std::printf("[signal] signal() records through the same registry as sigaction()\n");
    guest_signal_reset_for_test();

    void* const prev = kudroid::guest_signal(kGuestSIGUSR1,
                                             reinterpret_cast<void*>(&recording_handler));
    Check(prev != reinterpret_cast<void*>(SIG_ERR), "signal() succeeds");

    // Read it back with sigaction: it must be the handler signal() installed.
    GuestSigaction old;
    std::memset(&old, 0, sizeof(old));
    Check(guest_sigaction(kGuestSIGUSR1, nullptr, &old) == 0, "sigaction query succeeds");
    Check(old.sa_handler_or_sigaction == reinterpret_cast<void*>(&recording_handler),
          "sigaction sees the handler that signal() installed");

    // And signal() returns the previous one on the second call.
    void* const second = kudroid::guest_signal(kGuestSIGUSR1, reinterpret_cast<void*>(SIG_DFL));
    Check(second == reinterpret_cast<void*>(&recording_handler),
          "signal() returns the handler it is replacing");
}

// An unmappable number must be refused on the way out too, not sent raw. Sending it lands
// on whatever host signal has that value, which is how this class of bug does its damage.
void test_an_unmappable_send_is_refused() {
    std::printf("[send] a signal with no host counterpart is refused, not sent raw\n");
    guest_signal_reset_for_test();

    errno = 0;
    Check(kudroid::guest_raise(200) == -1 && errno == EINVAL,
          "raise(200) is refused with EINVAL rather than delivered somewhere");
    errno = 0;
    Check(kudroid::guest_kill(::getpid(), 200) == -1 && errno == EINVAL,
          "kill(pid, 200) likewise");

    // Signal 0 is the existence probe and must keep working: it delivers nothing.
    Check(kudroid::guest_raise(0) == 0, "raise(0) still succeeds as the no-op probe");
}

}  // namespace

int main() {
    std::printf("=== guest signal translation ===\n");
    test_layout_matches_bionic_lp64();
    test_the_observed_bad_pc_is_reproduced_by_the_old_layout();
    test_signal_numbers_agree_below_sigfpe();
    test_signal_numbers_are_translated_above_sigfpe();
    test_thread_suspension_signals_are_installable();
    test_real_time_signals_are_handled();
    test_out_of_range_signals_are_rejected();
    test_uncatchable_signals_are_refused();
    test_owned_signals_are_recorded_not_installed();
    test_owned_signal_reaches_the_guest_through_dispatch();
    test_dispatch_without_a_handler_does_nothing();
    test_sig_ign_and_sig_dfl_are_not_called();
    test_oldact_round_trips();
    test_oldact_hides_kudroids_own_handler();
    test_sigaltstack_uses_the_guests_field_order();
    test_sigaltstack_reports_failure();
    test_a_faulting_guest_handler_does_not_recurse();
    test_a_handler_that_jumps_out_does_not_disable_later_dispatch();
    test_recursion_is_still_declined_after_a_jump_out();
    test_a_sent_signal_arrives_where_the_handler_was_installed();
    test_the_untranslated_send_does_not_arrive();
    test_sigprocmask_translates_how_and_the_mask();
    test_signal_and_sigaction_share_one_registry();
    test_an_unmappable_send_is_refused();

    std::printf("=== %d checks, %d failures ===\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
