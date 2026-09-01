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
    kJavaMonitor,    // Object.wait / contended monitor-enter inside KuART
};

// Declare that this thread is entering a blocking wait. `object` is the address the
// guest is waiting on (futex word, sem_t, mutex, condvar) and is what makes two
// stuck threads distinguishable. `caller` should be __builtin_return_address(0) so
// the report can name the guest function; nullptr is accepted.
//
// Re-entrant declarations are not stacked: the innermost wins, which is correct
// because a thread can only be parked on one thing at a time and the innermost is
// the one actually blocking.
void blocking_wait_begin(WaitKind kind, const void* object, const void* caller);

// Clear this thread's declaration. Safe to call when nothing was declared.
void blocking_wait_end();

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
int blocking_wait_report_stalled(uint64_t threshold_ms);

// Number of waits currently outstanding. For tests, and for a summary line.
int blocking_wait_active_count();

// Test seam: drop all state. Not for use on a live guest.
void blocking_wait_reset_for_test();

}  // namespace kudroid

#endif  // KUDROID_ABI_BLOCKINGWAITREGISTRY_H
