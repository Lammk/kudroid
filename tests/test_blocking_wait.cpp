// test_blocking_wait.cpp — a guest thread parked forever must show up in the log.
//
// Why this exists. ULTRAKILL stopped inside UnityPlayer.nativeRender and the log
// simply ended: bionic_pthread_mutex_lock and bionic_futex log nothing per call
// (they are far too hot), and the one futex diagnostic that did exist was gated
// behind KUDROID_GUARD_DIAG — an environment variable nothing sets on iOS, so it had
// never run on the platform it was written for. Three rounds of reasoning about
// which shim was at fault produced three wrong answers, because the evidence needed
// to tell them apart was not being recorded.
//
// These tests drive the registry directly and, more importantly, drive it THROUGH
// the shims: a wait that is tracked in principle but not wired into bionic_sem_wait
// would pass a registry-only test and still leave the next hang invisible.
#include "kudroid/abi/BlockingWaitRegistry.h"
#include "kudroid/debug/ThreadSampler.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdint.h>

extern "C" {
int bionic_sem_init(sem_t* sem, int pshared, unsigned int value);
int bionic_sem_destroy(sem_t* sem);
int bionic_sem_wait(sem_t* sem);
int bionic_sem_post(sem_t* sem);
int bionic_futex(uint32_t* uaddr, int futex_op, uint32_t val,
                 const struct timespec* timeout, uint32_t* uaddr2, uint32_t val3);
int bionic_pthread_mutex_init(void* m, void* attr);
int bionic_pthread_mutex_lock(void* m);
int bionic_pthread_mutex_unlock(void* m);
int bionic_pthread_mutex_destroy(void* m);
int bionic_pthread_join(pthread_t thread, void** retval);
}

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    std::printf("%s %s\n", ok ? "  OK  " : "  FAIL", what.c_str());
    if (!ok) ++g_failures;
}

using kudroid::blocking_wait_active_count;
using kudroid::blocking_wait_begin;
using kudroid::blocking_wait_end;
using kudroid::blocking_wait_note_budget;
using kudroid::blocking_wait_note_iteration;
using kudroid::blocking_wait_note_owner;
using kudroid::blocking_wait_report_stalled;
using kudroid::blocking_wait_reset_for_test;
using kudroid::BlockingWaitScope;
using kudroid::guest_return_address;
using kudroid::thread_sample_report;
using kudroid::WaitKind;

constexpr int FUTEX_WAIT_OP = 0;
constexpr int FUTEX_WAKE_OP = 1;

// A wait shorter than the threshold must stay silent. This is the property that
// makes the registry affordable to leave enabled: a busy guest takes locks
// constantly and none of it may reach the log.
void test_short_wait_is_not_reported() {
    std::printf("[stall] a wait under the threshold is not reported\n");
    blocking_wait_reset_for_test();

    int object = 0;
    {
        const BlockingWaitScope tracked(WaitKind::kMutex, &object, nullptr);
        Check(blocking_wait_active_count() == 1, "the wait is registered while held");
        Check(blocking_wait_report_stalled(1000) == 0,
              "a wait that just started is not stalled");
    }
    Check(blocking_wait_active_count() == 0, "leaving the scope clears it");
    Check(blocking_wait_report_stalled(0) == 0,
          "and a completed wait is never reported, even at a zero threshold");
}

// The property the whole thing exists for.
void test_long_wait_is_reported_once() {
    std::printf("[stall] a wait past the threshold is reported exactly once\n");
    blocking_wait_reset_for_test();

    int object = 0;
    const BlockingWaitScope tracked(WaitKind::kFutex, &object, nullptr);
    std::this_thread::sleep_for(std::chrono::milliseconds(120));

    Check(blocking_wait_report_stalled(100) == 1, "a wait older than the threshold is reported");
    Check(blocking_wait_report_stalled(100) == 0,
          "and not again on the next scan — a permanently stuck thread costs one line, "
          "not four per second");
}

// Two threads stuck on different objects must be distinguishable; reporting only
// one would hide half of a deadlock.
void test_two_stalled_threads_both_reported() {
    std::printf("[stall] two stuck threads are both reported\n");
    blocking_wait_reset_for_test();

    std::atomic<bool> release{false};
    std::atomic<int> parked{0};
    int object_a = 0, object_b = 0;

    auto park = [&](int* object) {
        const BlockingWaitScope tracked(WaitKind::kMutex, object, nullptr);
        ++parked;
        while (!release.load()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    };
    std::thread a(park, &object_a);
    std::thread b(park, &object_b);
    while (parked.load() < 2) std::this_thread::yield();

    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    Check(blocking_wait_active_count() == 2, "both waits are registered");
    Check(blocking_wait_report_stalled(100) == 2, "both are reported");

    release = true;
    a.join();
    b.join();
    Check(blocking_wait_active_count() == 0, "both clear on the way out");
}

// A thread that keeps waiting, briefly, must not accumulate entries. Getting this
// wrong would leak a slot per wait and eventually stop tracking anything.
void test_slot_is_reused_across_waits() {
    std::printf("[stall] one thread reuses its slot across many waits\n");
    blocking_wait_reset_for_test();

    int object = 0;
    {
        const BlockingWaitScope tracked(WaitKind::kCondition, &object, nullptr);
        Check(blocking_wait_active_count() == 1, "exactly one wait outstanding");
    }
    for (int i = 0; i < 500; ++i) {
        const BlockingWaitScope tracked(WaitKind::kCondition, &object, nullptr);
    }
    Check(blocking_wait_active_count() == 0, "no entry leaked after 500 waits");
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    Check(blocking_wait_report_stalled(10) == 0, "and nothing is left to report");
}

// blocking_wait_end without a matching begin must not corrupt the count. The mutex
// path can reach it when the slot table was full at entry.
void test_unmatched_end_is_safe() {
    std::printf("[stall] an unmatched end does not corrupt the count\n");
    blocking_wait_reset_for_test();

    blocking_wait_end();
    blocking_wait_end();
    Check(blocking_wait_active_count() == 0, "the count stays at zero");

    int object = 0;
    blocking_wait_begin(WaitKind::kSemaphore, &object, nullptr);
    Check(blocking_wait_active_count() == 1, "a later real wait still registers");
    blocking_wait_end();
    blocking_wait_end();
    Check(blocking_wait_active_count() == 0, "and the extra end is ignored");
}

// ─── through the shims ───────────────────────────────────────────────────────
//
// The registry being correct is not the point. The point is that the waits a guest
// actually performs are wired into it.

void test_sem_wait_is_tracked() {
    std::printf("[stall] bionic_sem_wait registers its wait\n");
    blocking_wait_reset_for_test();

    sem_t sem;
    std::memset(&sem, 0, sizeof(sem));
    bionic_sem_init(&sem, 0, 0);

    std::atomic<bool> returned{false};
    std::thread waiter([&] {
        bionic_sem_wait(&sem);
        returned = true;
    });

    // Wait for the thread to be inside the wait, observed through the registry
    // rather than by sleeping a guessed interval.
    for (int i = 0; i < 200 && blocking_wait_active_count() == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Check(blocking_wait_active_count() == 1,
          "a thread inside sem_wait is visible — this is Unity's helper-thread handshake");

    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    Check(blocking_wait_report_stalled(100) == 1, "and is reported when it overruns");

    bionic_sem_post(&sem);
    waiter.join();
    Check(returned.load(), "the wait completed once posted");
    Check(blocking_wait_active_count() == 0, "and cleared its entry");
    bionic_sem_destroy(&sem);
}

void test_futex_wait_is_tracked() {
    std::printf("[stall] bionic_futex FUTEX_WAIT registers its wait\n");
    blocking_wait_reset_for_test();

    uint32_t word = 0;
    std::atomic<bool> returned{false};
    std::thread waiter([&] {
        bionic_futex(&word, FUTEX_WAIT_OP, 0, nullptr, nullptr, 0);
        returned = true;
    });

    for (int i = 0; i < 200 && blocking_wait_active_count() == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Check(blocking_wait_active_count() == 1, "a thread inside FUTEX_WAIT is visible");

    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    Check(blocking_wait_report_stalled(100) == 1, "and is reported when it overruns");

    word = 1;
    bionic_futex(&word, FUTEX_WAKE_OP, 1, nullptr, nullptr, 0);
    waiter.join();
    Check(returned.load(), "the wait completed once woken");
    Check(blocking_wait_active_count() == 0, "and cleared its entry");
}

void test_mutex_lock_is_tracked() {
    std::printf("[stall] bionic_pthread_mutex_lock registers a contended wait\n");
    blocking_wait_reset_for_test();

    // A guest pthread_mutex_t is an opaque word to the shim; 64 bytes is more than
    // bionic's and keeps the registry keyed on a stable address.
    alignas(8) unsigned char guest_mutex[64];
    std::memset(guest_mutex, 0, sizeof(guest_mutex));
    bionic_pthread_mutex_init(guest_mutex, nullptr);

    Check(bionic_pthread_mutex_lock(guest_mutex) == 0, "the first lock succeeds");
    Check(blocking_wait_active_count() == 0,
          "an uncontended lock leaves nothing registered — it did not block");

    std::atomic<bool> acquired{false};
    std::thread contender([&] {
        bionic_pthread_mutex_lock(guest_mutex);
        acquired = true;
        bionic_pthread_mutex_unlock(guest_mutex);
    });

    for (int i = 0; i < 200 && blocking_wait_active_count() == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Check(blocking_wait_active_count() == 1, "the blocked contender is visible");

    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    Check(blocking_wait_report_stalled(100) == 1,
          "a lock convoy that never clears is reported");

    bionic_pthread_mutex_unlock(guest_mutex);
    contender.join();
    Check(acquired.load(), "the contender got the mutex");
    Check(blocking_wait_active_count() == 0, "and cleared its entry");
    bionic_pthread_mutex_destroy(guest_mutex);
}

// ─── spin tracking ───────────────────────────────────────────────────────────
//
// A wait parks; a spin burns CPU. Nothing that looks for blocked threads can see a
// spin, which is how ULTRAKILL's main thread spent twelve seconds inside
// nativeRender with no wait registered anywhere: __cxa_guard_acquire yields and
// retries while another thread runs a static initialiser.
//
// Elapsed time alone cannot tell a spin from a legitimate long wait — an idle worker
// blocked on a semaphore also crosses three seconds. The iteration count is what
// separates them.

void test_spin_reports_its_iteration_count() {
    std::printf("[spin] a spin reports how many times it went round\n");
    blocking_wait_reset_for_test();

    int guard = 0;
    const BlockingWaitScope tracked(WaitKind::kGuardSpin, &guard, nullptr);
    for (int i = 0; i < 2500; ++i) blocking_wait_note_iteration();
    blocking_wait_note_owner(4242);

    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    Check(blocking_wait_report_stalled(100) == 1,
          "the spin is reported (iterations and owner appear in the line)");
    Check(blocking_wait_report_stalled(100) == 0, "and only once");
}

void test_iteration_and_owner_are_ignored_without_a_wait() {
    std::printf("[spin] noting progress outside a wait is harmless\n");
    blocking_wait_reset_for_test();

    // Both are called from loops that may not have registered — the slot table can
    // be full — so neither may touch state or crash.
    blocking_wait_note_iteration();
    blocking_wait_note_owner(7);
    Check(blocking_wait_active_count() == 0, "nothing was registered");

    int object = 0;
    {
        const BlockingWaitScope tracked(WaitKind::kGuardSpin, &object, nullptr);
        blocking_wait_note_iteration();
        Check(blocking_wait_active_count() == 1, "a real spin still registers");
    }
    blocking_wait_note_iteration();
    Check(blocking_wait_active_count() == 0, "and a late note does not resurrect it");
}

// A stall report naming KuDroid's own bionic_futex is worthless: that is where every
// futex wait in the process comes from. What identifies a bug is the guest frame.
void test_guest_return_address_falls_back_sanely() {
    std::printf("[spin] guest_return_address returns a usable address off-device\n");

    const void* a = guest_return_address(6);
    Check(a != nullptr,
          "with no guest module loaded it still yields the immediate caller rather "
          "than nothing");

    // Must not walk off the end of the stack for any frame count, including absurd
    // ones: this runs on a thread that is about to block, so a diagnostic that
    // crashes is worse than no diagnostic.
    Check(guest_return_address(0) != nullptr, "a zero frame count is clamped, not UB");
    Check(guest_return_address(1000) != nullptr, "a huge frame count is clamped too");
    Check(guest_return_address(-5) != nullptr, "a negative frame count is clamped too");
}

// The frame walk must survive being called from an arbitrary frame shape, on an
// arbitrary thread, at the optimisation level the product ships with.
//
// This is a regression test with a real failure behind it. The walk used to accept any
// frame pointer in [0x1000, 0x7fffffffffff] — which excludes null and nothing else. At
// -O3 the compiler may omit the frame pointer, so __builtin_frame_address(0) is not
// necessarily the base of a valid chain: the word read where a saved fp would be is
// whatever was in that stack slot, and the next iteration dereferences it. Adding one
// caller with a different frame shape (Monitor::Wait) turned that into a segfault on
// every run of test_kuart_libcore and roughly half the runs of test_shims — a
// diagnostic crashing the process it was meant to explain.
//
// Calling it many times from several depths on several threads is what would have
// caught it. A single call from main() did not.
void test_guest_return_address_survives_deep_and_threaded_calls() {
    std::printf("[spin] guest_return_address is safe from any frame depth, on any thread\n");

    // Recursion, so the walk is entered from a chain of frames of varying shape rather
    // than only from main's.
    struct Recurse {
        static const void* go(int depth) {
            if (depth == 0) return guest_return_address(12);
            // A live local the compiler cannot fold away, so each level really does
            // build a frame.
            volatile char pad[64];
            pad[0] = static_cast<char>(depth);
            const void* r = go(depth - 1);
            return r != nullptr ? r : reinterpret_cast<const void*>(pad[0]);
        }
    };

    bool all_non_null = true;
    for (int depth = 0; depth < 24; ++depth) {
        if (Recurse::go(depth) == nullptr) { all_non_null = false; break; }
    }
    Check(all_non_null, "24 nesting depths all return an address and none faults");

    // On a fresh thread, whose stack bounds are different from the main thread's — the
    // main thread's stack is the process stack and is queried differently on some
    // platforms, so a bounds check that works only there is not enough.
    std::atomic<int> ok{0};
    std::atomic<bool> faulted{false};
    auto worker = [&] {
        for (int i = 0; i < 200; ++i) {
            if (guest_return_address(12) == nullptr) { faulted = true; return; }
        }
        ok.fetch_add(1);
    };
    std::thread t1(worker);
    std::thread t2(worker);
    t1.join();
    t2.join();
    Check(!faulted.load() && ok.load() == 2,
          "400 calls across two fresh threads: no fault, every call answered");

    // And from inside a wait, which is the only place it is really called from.
    int object = 0;
    {
        const BlockingWaitScope tracked(WaitKind::kJavaMonitor, &object,
                                        guest_return_address(6));
        Check(blocking_wait_active_count() == 1,
              "a wait registered with an address taken the way the shims take it");
    }
    Check(blocking_wait_active_count() == 0, "and cleared normally");
}

// ─── budgets ─────────────────────────────────────────────────────────────────
//
// The captured ULTRAKILL log reported exactly one stall, and it was the wrong thread:
// an idle AssetGarbageCollectorHelper that had asked to wait and was waiting
// (waited_ms=3035, timeout op=128). The main thread — 36 seconds into a nativeRender
// that never returned — was not mentioned. One false line and one missing line, and
// the false one does more damage: it is what a reader chases.

void test_a_wait_inside_its_budget_is_not_stalled() {
    std::printf("[budget] a wait still inside the time it asked for is not stalled\n");
    blocking_wait_reset_for_test();

    int object = 0;
    const BlockingWaitScope tracked(WaitKind::kFutexTimed, &object, nullptr);
    blocking_wait_note_budget(60000);  // asked to wait a minute
    std::this_thread::sleep_for(std::chrono::milliseconds(120));

    Check(blocking_wait_report_stalled(100) == 0,
          "past the watchdog threshold but far inside its own budget: silent");
    Check(blocking_wait_active_count() == 1, "and the wait is still registered");
}

void test_a_wait_past_its_budget_is_stalled() {
    std::printf("[budget] a wait that overran the time it asked for is stalled\n");
    blocking_wait_reset_for_test();

    int object = 0;
    const BlockingWaitScope tracked(WaitKind::kFutexTimed, &object, nullptr);
    blocking_wait_note_budget(50);  // asked for 50ms
    std::this_thread::sleep_for(std::chrono::milliseconds(120));

    Check(blocking_wait_report_stalled(100) == 1,
          "over both the threshold and its own budget: reported");
}

void test_an_unbounded_wait_ignores_the_budget_rule() {
    std::printf("[budget] a wait that asked for no bound is judged on the threshold alone\n");
    blocking_wait_reset_for_test();

    int object = 0;
    const BlockingWaitScope tracked(WaitKind::kFutex, &object, nullptr);
    // No note_budget call at all — an untimed FUTEX_WAIT, which is the case that
    // genuinely waits forever.
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    Check(blocking_wait_report_stalled(100) == 1, "reported at the threshold");
}

void test_budget_is_cleared_between_waits() {
    std::printf("[budget] a budget does not leak into the thread's next wait\n");
    blocking_wait_reset_for_test();

    int object = 0;
    {
        const BlockingWaitScope tracked(WaitKind::kFutexTimed, &object, nullptr);
        blocking_wait_note_budget(60000);
    }
    // The same slot, reused. If the budget survived, this untimed wait would inherit a
    // minute of grace and the next real hang would go unreported.
    const BlockingWaitScope tracked(WaitKind::kFutex, &object, nullptr);
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    Check(blocking_wait_report_stalled(100) == 1,
          "the second wait is reported: the previous budget did not carry over");
}

void test_note_budget_without_a_wait_is_harmless() {
    std::printf("[budget] noting a budget outside a wait does nothing\n");
    blocking_wait_reset_for_test();

    blocking_wait_note_budget(1000);
    Check(blocking_wait_active_count() == 0, "nothing was registered");
}

// ─── pthread_join ────────────────────────────────────────────────────────────
//
// A join inherits whatever is blocking the target thread. It was bound straight to
// the host symbol with no wrapper, so a guest joining a thread that never returns
// produced no record of either half of the problem.

void test_join_is_tracked() {
    std::printf("[stall] bionic_pthread_join registers its wait\n");
    blocking_wait_reset_for_test();

    std::atomic<bool> release{false};
    // A raw pthread, because that is what bionic_pthread_join takes.
    struct Args {
        std::atomic<bool>* release;
    };
    Args args{&release};
    pthread_t worker;
    const int created = pthread_create(
        &worker, nullptr,
        [](void* raw) -> void* {
            Args* a = static_cast<Args*>(raw);
            while (!a->release->load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            return nullptr;
        },
        &args);
    Check(created == 0, "the worker started");

    std::atomic<bool> joined{false};
    std::thread joiner([&] {
        bionic_pthread_join(worker, nullptr);
        joined = true;
    });

    for (int i = 0; i < 400 && blocking_wait_active_count() == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Check(blocking_wait_active_count() == 1, "a thread inside pthread_join is visible");

    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    Check(blocking_wait_report_stalled(100) == 1,
          "a join on a thread that will not exit is reported");

    release = true;
    joiner.join();
    Check(joined.load(), "the join returned once the worker exited");
    Check(blocking_wait_active_count() == 0, "and cleared its entry");
}

// ─── thread sampler ──────────────────────────────────────────────────────────
//
// The sampler answers the question every other diagnostic here cannot: where is a
// thread that is stuck WITHOUT calling any of our code. On this host there is no
// cross-thread register access, so what is checked is that it reports its own
// unavailability rather than crashing or lying — a sampler that segfaulted while
// explaining a hang would be worse than none.

void test_thread_sampler_is_safe_to_call() {
    std::printf("[sample] the thread sampler is safe to call and honest when it cannot read\n");

    const int n = thread_sample_report("test");
    // On Apple arm64 this reports at least this thread; elsewhere it reports nothing
    // and says so. Both are correct; a crash or a fabricated number is not.
    Check(n >= 0, "it returns a count, not a crash");
#if defined(__APPLE__) && defined(__aarch64__)
    Check(n >= 1, "on Apple arm64 it sees at least the calling thread");
#else
    Check(n == 0, "off Apple arm64 it reports nothing rather than guessing");
#endif

    // Repeated calls must stay bounded and not accumulate state: the watchdog calls
    // this every ten seconds for as long as a call is wedged.
    for (int i = 0; i < 20; ++i) thread_sample_report("repeat");
    Check(true, "twenty repeat samples completed without incident");
}

}  // namespace

int main() {
    std::printf("=== blocking wait registry ===\n");
    test_short_wait_is_not_reported();
    test_long_wait_is_reported_once();
    test_two_stalled_threads_both_reported();
    test_slot_is_reused_across_waits();
    test_unmatched_end_is_safe();
    test_sem_wait_is_tracked();
    test_futex_wait_is_tracked();
    test_mutex_lock_is_tracked();
    test_spin_reports_its_iteration_count();
    test_iteration_and_owner_are_ignored_without_a_wait();
    test_guest_return_address_falls_back_sanely();
    test_guest_return_address_survives_deep_and_threaded_calls();
    test_a_wait_inside_its_budget_is_not_stalled();
    test_a_wait_past_its_budget_is_stalled();
    test_an_unbounded_wait_ignores_the_budget_rule();
    test_budget_is_cleared_between_waits();
    test_note_budget_without_a_wait_is_harmless();
    test_join_is_tracked();
    test_thread_sampler_is_safe_to_call();

    std::printf("=== %d checks, %d failures ===\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
