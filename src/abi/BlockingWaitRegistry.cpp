#include "kudroid/abi/BlockingWaitRegistry.h"
#include "kudroid/debug/FrameWalk.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <pthread.h>
#if !defined(__APPLE__)
#include <sys/syscall.h>
#include <unistd.h>
#endif

extern "C" void kudroid_persistent_breadcrumb(const char* line);
extern "C" int kudroid_android_log_message(int priority, const char* tag, const char* message);

namespace kudroid {

// Resolving a guest address to "module+offset" lives in elf_loader. Declared rather
// than included so this file stays free of the loader's headers.
extern "C" bool kudroid_lookup_guest_module(void* addr, char* out, std::size_t outSize);

namespace {

// A fixed table rather than a map keyed by thread id.
//
// The scan runs from the watchdog while the threads it inspects are blocked, so it
// must not take a lock those threads could be holding, and it must not allocate.
// A fixed array of atomics gives a wait-free write on the guest's path and a
// lock-free scan on the watchdog's. 256 slots is far more threads than a guest
// engine runs; overflow degrades to "not tracked", never to a wrong answer.
constexpr int kMaxSlots = 256;

struct Slot {
    // claimed: this slot belongs to a thread. Owned by the thread; only it clears.
    std::atomic<bool> claimed{false};
    // in_wait: that thread is inside a blocking wait right now. Separate from
    // claimed so a thread keeps its slot across many waits instead of racing for a
    // new one each time.
    std::atomic<bool> in_wait{false};
    std::atomic<int> kind{static_cast<int>(WaitKind::kNone)};
    std::atomic<const void*> object{nullptr};
    std::atomic<const void*> caller{nullptr};
    std::atomic<uint64_t> started_ns{0};
    std::atomic<uint64_t> thread_id{0};
    // Retry count, for a spin that has no single blocking call to sit inside.
    std::atomic<uint64_t> iterations{0};
    // Whoever holds the thing being waited for, when the wait can know it. Zero
    // means unknown, which is the honest answer for a futex word or a mutex whose
    // owner the shim does not track.
    std::atomic<uint64_t> owner{0};
    // How long this wait was asked to take, when it asked for a bound. Zero means
    // unbounded. A wait inside its own budget is not stalled however long the budget
    // is — see blocking_wait_note_budget.
    std::atomic<uint64_t> budget_ms{0};
    // Set once a stall has been reported, so a permanently stuck thread produces
    // one line rather than one every watchdog tick.
    std::atomic<bool> reported{false};
    // Same, for the idle report, which uses a different threshold and severity.
    std::atomic<bool> idle_reported{false};
};

Slot g_slots[kMaxSlots];
std::atomic<int> g_active{0};

// This thread's slot. Assigned on first wait and kept for the thread's lifetime:
// churning slots would make the scan see a torn view.
thread_local Slot* t_slot = nullptr;

uint64_t now_ns() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

uint64_t self_thread_id() {
#if defined(__APPLE__)
    uint64_t tid = 0;
    ::pthread_threadid_np(nullptr, &tid);
    return tid;
#else
    return static_cast<uint64_t>(::syscall(SYS_gettid));
#endif
}

const char* kind_name(WaitKind kind) {
    switch (kind) {
        case WaitKind::kNone: return "none";
        case WaitKind::kFutex: return "futex-wait";
        case WaitKind::kFutexTimed: return "futex-wait-timed";
        case WaitKind::kSemaphore: return "sem-wait";
        case WaitKind::kSemaphoreTimed: return "sem-timedwait";
        case WaitKind::kMutex: return "mutex-lock";
        case WaitKind::kCondition: return "cond-wait";
        case WaitKind::kConditionTimed: return "cond-timedwait";
        case WaitKind::kJavaMonitor: return "java-monitor";
        case WaitKind::kJavaWait: return "java-wait";
        case WaitKind::kRwlockRead: return "rwlock-rdlock";
        case WaitKind::kRwlockWrite: return "rwlock-wrlock";
        case WaitKind::kOnce: return "pthread-once";
        case WaitKind::kEpoll: return "epoll-wait";
        case WaitKind::kJoin: return "pthread-join";
        case WaitKind::kGuardSpin: return "cxa-guard-spin";
    }
    return "?";
}

// Stack bounds and the frame walk itself now live in debug/FrameWalk.h.
//
// They were written here first, because this is where the plausibility-test bug was found
// the first time: accepting any fp in [0x1000, 0x7fffffffffff] excludes null and nothing
// else, so at -O3 — where the frame pointer may be omitted and `*fp` yields whatever was
// in that slot — the walk followed garbage and segfaulted inside a diagnostic. It crashed
// test_kuart_libcore every run.
//
// The fix stayed local, and that was the mistake. Three other frame walks kept the
// plausibility test, and one of them killed a device run months later: the guard
// diagnostic in SyscallShim faulted at 0x100000000050, an address that passes
// `> 0x1000 && < 0x7fffffffffff` and is nowhere near a stack, on the guest's main thread.
// One definition of a valid frame, in one place, is what stops that from recurring.

// Claim a slot for this thread. Returns nullptr when the table is full, which the
// callers treat as "do not track" rather than as an error: losing a diagnostic is
// acceptable, refusing to let the guest wait is not.
Slot* acquire_slot() {
    for (int i = 0; i < kMaxSlots; ++i) {
        bool expected = false;
        if (g_slots[i].claimed.compare_exchange_strong(expected, true,
                                                       std::memory_order_acq_rel)) {
            g_slots[i].thread_id.store(self_thread_id(), std::memory_order_relaxed);
            return &g_slots[i];
        }
    }
    return nullptr;
}

}  // namespace

const void* guest_return_address(int frames) {
    // Walk the frame chain looking for the first address inside a loaded guest
    // module. The immediate return address is almost always KuDroid's own wrapper —
    // bionic_futex reached through emulate_futex_direct, for instance — and a report
    // naming that says nothing: every futex wait in the process comes from there.
    //
    // Every step is bounded by the calling thread's REAL stack, not by a plausibility
    // check. This runs on a thread that is about to block, and a diagnostic that
    // segfaults is worse than no diagnostic — which is exactly what the earlier version
    // did once a caller with a different frame shape appeared.
#if defined(__aarch64__) || defined(__x86_64__)
    if (frames < 1) frames = 1;
    if (frames > 12) frames = 12;

    // Cached, because this runs on every wait and pthread_getattr_np allocates on Linux.
    const StackBounds& stack = cached_thread_stack_bounds();
    if (!stack.valid) {
        // No bounds means no safe walk. The immediate caller is still true and costs
        // nothing to obtain.
        return __builtin_return_address(0);
    }

    FrameWalker walker(reinterpret_cast<uintptr_t>(__builtin_frame_address(0)), stack);
    const void* first = nullptr;
    char scratch[256];

    for (int i = 0; i < frames && walker.valid(); ++i) {
        uintptr_t ret = 0;
        if (!walker.return_address(&ret)) break;
        if (first == nullptr) first = reinterpret_cast<const void*>(ret);
        if (kudroid_lookup_guest_module(reinterpret_cast<void*>(ret), scratch,
                                        sizeof(scratch))) {
            return reinterpret_cast<const void*>(ret);
        }
        if (!walker.next()) break;
    }
    // No guest frame in range: report the immediate caller rather than nothing, so
    // the address is at least a starting point.
    return first != nullptr ? first : __builtin_return_address(0);
#else
    (void)frames;
    return __builtin_return_address(0);
#endif
}

void blocking_wait_begin(WaitKind kind, const void* object, const void* caller) {
    if (t_slot == nullptr) {
        t_slot = acquire_slot();
        if (t_slot == nullptr) return;  // table full: not tracked
    }
    Slot* s = t_slot;
    // Order matters. Every field is published BEFORE in_wait goes true, so the
    // watchdog can never read a half-written entry: it only looks at slots whose
    // in_wait it has already observed as true, and acquire/release pairs that with
    // these stores.
    s->kind.store(static_cast<int>(kind), std::memory_order_relaxed);
    s->object.store(object, std::memory_order_relaxed);
    s->caller.store(caller, std::memory_order_relaxed);
    s->started_ns.store(now_ns(), std::memory_order_relaxed);
    s->iterations.store(0, std::memory_order_relaxed);
    s->owner.store(0, std::memory_order_relaxed);
    s->budget_ms.store(0, std::memory_order_relaxed);
    s->reported.store(false, std::memory_order_relaxed);
    s->idle_reported.store(false, std::memory_order_relaxed);
    s->in_wait.store(true, std::memory_order_release);
    g_active.fetch_add(1, std::memory_order_relaxed);
}

void blocking_wait_note_iteration() {
    Slot* s = t_slot;
    if (s == nullptr) return;
    if (!s->in_wait.load(std::memory_order_relaxed)) return;
    s->iterations.fetch_add(1, std::memory_order_relaxed);
}

void blocking_wait_note_owner(uint64_t owner) {
    Slot* s = t_slot;
    if (s == nullptr) return;
    if (!s->in_wait.load(std::memory_order_relaxed)) return;
    s->owner.store(owner, std::memory_order_relaxed);
}

void blocking_wait_note_budget(uint64_t budget_ms) {
    Slot* s = t_slot;
    if (s == nullptr) return;
    if (!s->in_wait.load(std::memory_order_relaxed)) return;
    s->budget_ms.store(budget_ms, std::memory_order_relaxed);
}

void blocking_wait_end() {
    Slot* s = t_slot;
    if (s == nullptr) return;
    // Only decrement for a wait that was actually recorded. blocking_wait_end may be
    // reached without a matching begin when the table was full at entry.
    if (s->in_wait.exchange(false, std::memory_order_acq_rel)) {
        g_active.fetch_sub(1, std::memory_order_relaxed);
        // A wait that completed after being reported as stalled is worth saying:
        // it distinguishes "slow" from "hung", and the difference decides whether
        // there is a bug at all.
        if (s->reported.load(std::memory_order_relaxed)) {
            const uint64_t elapsed_ms =
                (now_ns() - s->started_ns.load(std::memory_order_relaxed)) / 1000000ull;
            char line[256];
            std::snprintf(line, sizeof(line),
                          "blocking-wait-resolved kind=%s object=%p tid=%llu waited_ms=%llu",
                          kind_name(static_cast<WaitKind>(
                              s->kind.load(std::memory_order_relaxed))),
                          s->object.load(std::memory_order_relaxed),
                          static_cast<unsigned long long>(
                              s->thread_id.load(std::memory_order_relaxed)),
                          static_cast<unsigned long long>(elapsed_ms));
            kudroid_persistent_breadcrumb(line);
            kudroid_android_log_message(4, "KuDroidStall", line);
        }
    }
}

int blocking_wait_report_stalled(uint64_t threshold_ms) {
    const uint64_t now = now_ns();
    int reported = 0;
    for (int i = 0; i < kMaxSlots; ++i) {
        Slot& s = g_slots[i];
        if (!s.in_wait.load(std::memory_order_acquire)) continue;
        if (s.reported.load(std::memory_order_relaxed)) continue;

        const uint64_t started = s.started_ns.load(std::memory_order_relaxed);
        if (started == 0 || now < started) continue;
        const uint64_t elapsed_ms = (now - started) / 1000000ull;
        if (elapsed_ms < threshold_ms) continue;

        // A wait still inside the bound it asked for is not stalled, however long
        // that bound is.
        //
        // This is what kept the captured ULTRAKILL log from being readable: the only
        // stall reported was an idle AssetGarbageCollectorHelper that had asked to
        // wait and was waiting, while the main thread — genuinely wedged, 36 seconds
        // into a nativeRender that never returned — produced nothing. One false line
        // and one missing line, and the false one is worse: it is what a reader
        // chases.
        const uint64_t budget = s.budget_ms.load(std::memory_order_relaxed);
        if (budget != 0 && elapsed_ms < budget) continue;

        // An unbounded Object.wait() is a thread with nothing to do, not a stall.
        //
        // This is the other false line from that log, and the worse of the two because
        // it came first:
        //
        //   blocking-wait-stalled kind=java-monitor object=0x12089e3d0 tid=3711750
        //     waited_ms=3060 budget_ms=0 iterations=0 owner=0
        //
        // An idle HandlerThread in MessageQueue.next(), which calls this.wait() with no
        // timeout when the queue is empty — precisely what Android's MessageQueue does
        // with nativePollOnce(-1). cpu_ms=0 across every sample. It was working
        // correctly, and it was reported at the same severity as a real hang with
        // owner=0, so there was nothing to follow and nothing to rule out.
        //
        // Any Java thread with an idle Looper trips this, in every app, forever. Left
        // in place it does not merely add noise: it trains a reader to skip the first
        // stall line, which is where a real one usually appears.
        //
        // Reported through blocking_wait_report_idle() instead, at a longer threshold
        // and a lower severity.
        //
        // Only the UNBOUNDED case. A timed wait that overran its own deadline is a
        // genuine anomaly and stays here: the thread asked to be woken by a point in
        // time that has passed, so either the notify never came and the timeout did not
        // fire, or it woke and could not retake the monitor. Both are real, and both are
        // invisible if the exemption is written on the kind alone. Reaching this line at
        // all means the budget check above already passed, so the deadline is behind us.
        if (static_cast<WaitKind>(s.kind.load(std::memory_order_relaxed)) ==
                WaitKind::kJavaWait &&
            budget == 0) {
            continue;
        }

        // Claim the report so two watchdogs cannot both emit it.
        bool expected = false;
        if (!s.reported.compare_exchange_strong(expected, true,
                                                std::memory_order_acq_rel)) {
            continue;
        }

        // Re-read in_wait after claiming: the thread may have woken between the
        // check above and here, in which case the entry is stale and reporting it
        // would be a false alarm.
        if (!s.in_wait.load(std::memory_order_acquire)) continue;

        const void* caller = s.caller.load(std::memory_order_relaxed);
        char where[256] = "unknown";
        if (caller != nullptr) {
            if (!kudroid_lookup_guest_module(const_cast<void*>(caller), where,
                                             sizeof(where))) {
                std::snprintf(where, sizeof(where), "%p (not a guest module)", caller);
            }
        }

        char line[576];
        const uint64_t iterations = s.iterations.load(std::memory_order_relaxed);
        std::snprintf(line, sizeof(line),
                      "blocking-wait-stalled kind=%s object=%p tid=%llu waited_ms=%llu "
                      "budget_ms=%llu iterations=%llu owner=%llu caller=%s",
                      kind_name(static_cast<WaitKind>(s.kind.load(std::memory_order_relaxed))),
                      s.object.load(std::memory_order_relaxed),
                      static_cast<unsigned long long>(s.thread_id.load(std::memory_order_relaxed)),
                      static_cast<unsigned long long>(elapsed_ms),
                      static_cast<unsigned long long>(budget),
                      static_cast<unsigned long long>(iterations),
                      static_cast<unsigned long long>(s.owner.load(std::memory_order_relaxed)),
                      where);
        kudroid_persistent_breadcrumb(line);
        // Also to the Android log: that is the file a user attaches to a report,
        // and a stall is exactly what they are reporting.
        kudroid_android_log_message(5, "KuDroidStall", line);
        ++reported;
    }
    return reported;
}

int blocking_wait_active_count() { return g_active.load(std::memory_order_relaxed); }

// Long-idle waits: a thread that parked itself with no deadline and nothing owing.
//
// Reported at priority 4 (info) rather than 5 (warn), under its own name, and at a
// threshold far above the stall one. All three differences matter: this is normal,
// and the reader has to be able to see that at a glance without reading the fields.
int blocking_wait_report_idle(uint64_t threshold_ms) {
    const uint64_t now = now_ns();
    int reported = 0;
    for (int i = 0; i < kMaxSlots; ++i) {
        Slot& s = g_slots[i];
        if (!s.in_wait.load(std::memory_order_acquire)) continue;
        if (s.idle_reported.load(std::memory_order_relaxed)) continue;
        if (static_cast<WaitKind>(s.kind.load(std::memory_order_relaxed)) !=
            WaitKind::kJavaWait) {
            continue;
        }

        const uint64_t started = s.started_ns.load(std::memory_order_relaxed);
        if (started == 0 || now < started) continue;
        const uint64_t elapsed_ms = (now - started) / 1000000ull;
        if (elapsed_ms < threshold_ms) continue;

        // A timed wait belongs to the stall scan, not here: overrunning a deadline it
        // set itself is an anomaly, and the two scans must not both claim it.
        const uint64_t budget = s.budget_ms.load(std::memory_order_relaxed);
        if (budget != 0) continue;

        bool expected = false;
        if (!s.idle_reported.compare_exchange_strong(expected, true,
                                                     std::memory_order_acq_rel)) {
            continue;
        }
        if (!s.in_wait.load(std::memory_order_acquire)) continue;

        const void* caller = s.caller.load(std::memory_order_relaxed);
        char where[256] = "unknown";
        if (caller != nullptr) {
            if (!kudroid_lookup_guest_module(const_cast<void*>(caller), where,
                                             sizeof(where))) {
                std::snprintf(where, sizeof(where), "%p (not a guest module)", caller);
            }
        }

        char line[576];
        std::snprintf(line, sizeof(line),
                      "blocking-wait-idle kind=%s object=%p tid=%llu waited_ms=%llu "
                      "budget_ms=%llu caller=%s (parked with no deadline; not a stall)",
                      kind_name(static_cast<WaitKind>(s.kind.load(std::memory_order_relaxed))),
                      s.object.load(std::memory_order_relaxed),
                      static_cast<unsigned long long>(s.thread_id.load(std::memory_order_relaxed)),
                      static_cast<unsigned long long>(elapsed_ms),
                      static_cast<unsigned long long>(budget), where);
        kudroid_persistent_breadcrumb(line);
        kudroid_android_log_message(4, "KuDroidIdle", line);
        ++reported;
    }
    return reported;
}

void blocking_wait_reset_for_test() {    for (int i = 0; i < kMaxSlots; ++i) {
        g_slots[i].in_wait.store(false, std::memory_order_relaxed);
        g_slots[i].claimed.store(false, std::memory_order_relaxed);
        g_slots[i].reported.store(false, std::memory_order_relaxed);
        g_slots[i].idle_reported.store(false, std::memory_order_relaxed);
        g_slots[i].kind.store(static_cast<int>(WaitKind::kNone), std::memory_order_relaxed);
        g_slots[i].object.store(nullptr, std::memory_order_relaxed);
        g_slots[i].caller.store(nullptr, std::memory_order_relaxed);
        g_slots[i].started_ns.store(0, std::memory_order_relaxed);
        g_slots[i].iterations.store(0, std::memory_order_relaxed);
        g_slots[i].owner.store(0, std::memory_order_relaxed);
        g_slots[i].budget_ms.store(0, std::memory_order_relaxed);
    }
    g_active.store(0, std::memory_order_relaxed);
    t_slot = nullptr;
}

}  // namespace kudroid
