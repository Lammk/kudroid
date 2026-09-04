// Blocking-wait registry: makes a hung guest thread visible in the log.
//
// The problem this solves. A guest that stops making progress leaves no trace:
// bionic_pthread_mutex_lock and bionic_futex log nothing (deliberately — they are
// far too hot to log per call), and the futex diagnostic that does exist is gated
// behind KUDROID_GUARD_DIAG, an environment variable nothing sets on iOS. So when
// ULTRAKILL stopped inside nativeRender the log simply ended, and three rounds of
// reasoning about which shim might be at fault produced three wrong answers.
//
// What it records. A thread about to enter a wait that can block indefinitely
// declares it — kind, the object's address, and when it started. On the way out the
// declaration is cleared. The telemetry watchdog, which already wakes every 250ms,
// scans the table and reports any wait older than a few seconds, resolving the
// caller's return address to a guest module and offset. One report per stuck wait,
// so a permanently wedged thread costs one line rather than four per second.
//
// Cost when nothing is wrong: two stores into a thread_local on the way in, one on
// the way out. No allocation, no lock, no syscall. That is what makes it acceptable
// to leave enabled in a release build — a diagnostic that must be switched on is a
// diagnostic that is off when the failure happens, which is exactly what went wrong
// with the futex gate.
#ifndef KUDROID_ABI_BLOCKINGWAITREGISTRY_H
#define KUDROID_ABI_BLOCKINGWAITREGISTRY_H

#include <cstdint>

namespace kudroid {

// Which primitive a thread is parked on. Named rather than a string so entering a
// wait cannot allocate or format.
enum class WaitKind : int {
    kNone = 0,
    kFutex,          // FUTEX_WAIT with no timeout — waits forever by construction
    kFutexTimed,     // FUTEX_WAIT with a timeout; reported only if it overruns badly
    kSemaphore,      // sem_wait
    kSemaphoreTimed, // sem_timedwait
    kMutex,          // pthread_mutex_lock
    kCondition,      // pthread_cond_wait (no timeout)
    kConditionTimed, // pthread_cond_timedwait
    // A CONTENDED monitor-enter inside KuART: this thread wants a monitor another
    // thread holds. Stuck iff the holder is stuck, and `owner` names it.
    kJavaMonitor,
    // Object.wait(): the monitor was RELEASED and the thread is parked until someone
    // notifies it. Distinct from kJavaMonitor because the two mean opposite things to
    // a reader, and conflating them produced the single most misleading line in the
    // captured ULTRAKILL log:
    //
    //   blocking-wait-stalled kind=java-monitor object=0x12089e3d0 tid=3711750
    //     waited_ms=3060 budget_ms=0 owner=0
    //
    // That was an idle HandlerThread sitting in MessageQueue.next(), which calls
    // this.wait() with no timeout when the queue is empty — exactly what Android's
    // own MessageQueue does with nativePollOnce(-1). Nothing was wrong with it. But
    // it was the FIRST stall line in the log, carried the same severity as a real
    // one, and `owner=0` gave nothing to follow, so it is what got chased while the
    // genuinely wedged main thread went unmentioned.
    //
    // An unbounded Object.wait() is reported through the idle path instead of the
    // stall path — see blocking_wait_report_idle().
    kJavaWait,
    kRwlockRead,     // pthread_rwlock_rdlock
    kRwlockWrite,    // pthread_rwlock_wrlock
    kOnce,           // pthread_once, waiting for another thread's initialiser
    kEpoll,          // epoll_wait with an indefinite timeout
    // pthread_join: waits for another thread to exit, and therefore inherits whatever
    // is blocking that thread. A join is the one wait whose report is only half the
    // story on its own — the other half is the target's own stalled line.
    kJoin,
    // A SPIN, not a wait: __cxa_guard_acquire yields and retries while another
    // thread runs a C++ static initialiser. Tracked with the waits because the
    // question a stalled report answers is the same one — "what is this thread
    // stuck on" — but it burns CPU instead of parking, so nothing that looks for
    // blocked threads can see it. That is exactly how ULTRAKILL's main thread
    // spent twelve seconds inside nativeRender with no wait registered anywhere.
    kGuardSpin,
};

// Declare that this thread is entering a blocking wait. `object` is the address the
// guest is waiting on (futex word, sem_t, mutex, condvar) and is what makes two
// stuck threads distinguishable. `caller` should be the guest address that asked for
// the wait — see guest_return_address() below, because the immediate return address
// is usually inside KuDroid's own wrapper and therefore useless in a report.
//
// Re-entrant declarations are not stacked: the innermost wins, which is correct
// because a thread can only be parked on one thing at a time and the innermost is
// the one actually blocking.
void blocking_wait_begin(WaitKind kind, const void* object, const void* caller);

// Clear this thread's declaration. Safe to call when nothing was declared.
void blocking_wait_end();

// Note that a tracked wait is still going round its retry loop. Only meaningful for
// kGuardSpin, where there is no single blocking call to sit inside: the count is
// what distinguishes "briefly contended" from "spinning forever", and it appears in
// the stalled report.
void blocking_wait_note_iteration();

// Attach one number to this thread's current wait, reported as owner=N.
//
// For kGuardSpin it is the tid recorded in the guard word — the thread running the
// static initialiser. That is the mssing half of a stall report: knowing a thread is
// spinning on guard X is not actionable, but "spinning on X, owned by tid Y" pairs
// directly with Y's own stalled line and names the whole cycle.
void blocking_wait_note_owner(uint64_t owner);

// Declare how long this wait was ASKED to take, so overrunning can be told from
// simply being long.
//
// Without this, a timed wait is judged against a fixed threshold, and that produced
// the one false positive in the captured ULTRAKILL log: an idle
// AssetGarbageCollectorHelper parked on a futex with a long timeout was reported as
// "stalled" at 3035ms while the actually-wedged main thread was reported not at all.
// A thread that asked to wait a minute and has waited three seconds is doing exactly
// what it asked for, and saying otherwise trains a reader to ignore the line.
//
// With a budget set, the wait is reported only once it passes BOTH the caller's
// threshold and its own budget. Zero, the default, means "no bound was requested" —
// which for an untimed wait is the truth, and those are judged on the threshold
// alone.
void blocking_wait_note_budget(uint64_t budget_ms);

// The first return address that lies inside a guest module, or the immediate caller
// when none does.
//
// A report naming KuDroid's own bionic_futex is worthless — that is where every
// futex wait comes from. What identifies the bug is the guest frame that asked for
// it. `frames` is how many levels to walk; the walk stops at the first address that
// resolves to a loaded guest module.
const void* guest_return_address(int frames);

// RAII form. Prefer this: an early return out of a wait path that forgot to call
// blocking_wait_end would leave a phantom entry and produce a false report.
class BlockingWaitScope {
public:
    BlockingWaitScope(WaitKind kind, const void* object, const void* caller) {
        blocking_wait_begin(kind, object, caller);
    }
    ~BlockingWaitScope() { blocking_wait_end(); }
    BlockingWaitScope(const BlockingWaitScope&) = delete;
    BlockingWaitScope& operator=(const BlockingWaitScope&) = delete;
};

// Report every wait that has been outstanding longer than `threshold_ms`, once per
// wait. Called from the telemetry watchdog thread; returns how many it reported so
// the caller can tell "nothing is stuck" from "the scan did not run".
//
// A wait a thread PARKED ITSELF in with no deadline and no owner — kJavaWait, an
// unbounded Object.wait() — is not reported here. That is not a stall; it is a thread
// with nothing to do, and it is indistinguishable from one only by what it is waiting
// for. See blocking_wait_report_idle().
int blocking_wait_report_stalled(uint64_t threshold_ms);

// Report long-idle waits, at a lower severity and a much higher threshold.
//
// Kept separate rather than dropped, because "which threads are asleep and on what"
// is genuinely useful when a deadlock involves one of them — a notifier that died is
// visible only as its waiter never being woken. What it must not do is share a
// severity with a real stall: the captured ULTRAKILL log opened with an idle
// HandlerThread reported as stalled, and that line is what a reader follows first.
//
// Returns how many it reported.
int blocking_wait_report_idle(uint64_t threshold_ms);

// Number of waits currently outstanding. For tests, and for a summary line.
int blocking_wait_active_count();

// Test seam: drop all state. Not for use on a live guest.
void blocking_wait_reset_for_test();

}  // namespace kudroid

#endif  // KUDROID_ABI_BLOCKINGWAITREGISTRY_H
