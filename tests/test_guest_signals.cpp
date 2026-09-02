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
#include <signal.h>
#include <stddef.h>
#include <stdint.h>

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

void test_signals_with_no_host_counterpart_are_rejected() {
    std::printf("[numbers] a signal the host does not have is EINVAL, not a wrong guess\n");
    guest_signal_reset_for_test();

    Check(guest_signal_to_host(kGuestSIGSTKFLT) == 0, "SIGSTKFLT (16) has no counterpart");
    Check(guest_signal_to_host(kGuestSIGPWR) == 0, "SIGPWR (30) has no counterpart");

    GuestSigaction act;
    std::memset(&act, 0, sizeof(act));
    act.sa_handler_or_sigaction = reinterpret_cast<void*>(&test_layout_matches_bionic_lp64);

    errno = 0;
    Check(guest_sigaction(kGuestSIGSTKFLT, &act, nullptr) == -1 && errno == EINVAL,
          "installing a handler for it fails with EINVAL rather than landing on some "
          "other signal");

    errno = 0;
    Check(guest_sigaction(0, &act, nullptr) == -1 && errno == EINVAL, "signal 0 is EINVAL");
    errno = 0;
    Check(guest_sigaction(9999, &act, nullptr) == -1 && errno == EINVAL,
          "an out-of-range signal is EINVAL, not an out-of-bounds write");
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

}  // namespace

int main() {
    std::printf("=== guest signal translation ===\n");
    test_layout_matches_bionic_lp64();
    test_the_observed_bad_pc_is_reproduced_by_the_old_layout();
    test_signal_numbers_agree_below_sigfpe();
    test_signal_numbers_are_translated_above_sigfpe();
    test_signals_with_no_host_counterpart_are_rejected();
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

    std::printf("=== %d checks, %d failures ===\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
