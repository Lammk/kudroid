#include "kudroid/kuart/VmLock.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <pthread.h>
#if !defined(__APPLE__)
#include <sys/syscall.h>
#include <unistd.h>
#endif

#include "kudroid/abi/BlockingWaitRegistry.h"
#include "kudroid/kuart/DexObject.h"

namespace kudroid {
namespace kuart {

namespace {

std::recursive_mutex g_vm_lock;

// Recursion depth of this thread inside g_vm_lock. Needed because the main
// thread enters bytecode without going through VmLockGuard (it is called from
// the iOS host), so VmLockRelease must not unlock a mutex it never acquired.
thread_local int t_vm_lock_depth = 0;

// Guards every DexObject monitor. One mutex for all objects is enough: the VM
// lock already serialises bytecode, so contention here is only between a waiter
// and a notifier.
std::mutex g_monitor_mutex;
std::condition_variable g_monitor_cv;

thread_local uint32_t t_thread_id = 0;

// KuART thread id → operating-system thread id.
//
// A monitor records its holder as DexObject::lock_owner_tid, which is a dense counter
// starting at 1 — not an OS tid. Reporting that number as owner= would be worse than
// reporting nothing: every other diagnostic KuDroid prints uses the real 64-bit tid
// (the registry's tid=, the futex line's, the thread sampler's), so a small integer in
// the same field reads as a tid and joins to the wrong thread, or to none.
//
// The ids are dense and small by construction, so a fixed array indexed by id is both
// exact and allocation-free. Overflow past the array degrades to owner=0, "unknown",
// which is the honest answer.
constexpr uint32_t kMaxKuartThreads = 256;
std::atomic<uint64_t> g_os_tid_by_kuart_id[kMaxKuartThreads];

uint64_t os_thread_id() {
#if defined(__APPLE__)
    uint64_t tid = 0;
    ::pthread_threadid_np(nullptr, &tid);
    return tid;
#else
    return static_cast<uint64_t>(::syscall(SYS_gettid));
#endif
}

// The OS tid of whichever thread holds KuART id `id`, or 0 when unknown.
uint64_t os_tid_for(uint32_t id) {
    if (id == 0 || id >= kMaxKuartThreads) return 0;
    return g_os_tid_by_kuart_id[id].load(std::memory_order_relaxed);
}

}  // namespace

VmLockGuard::VmLockGuard() {
    g_vm_lock.lock();
    ++t_vm_lock_depth;
}

VmLockGuard::~VmLockGuard() {
    --t_vm_lock_depth;
    g_vm_lock.unlock();
}

VmLockRelease::VmLockRelease() : depth_(t_vm_lock_depth) {
    // The counter must go to zero while the lock is released, not merely be
    // remembered. VmLockDepth() answers "does this thread hold the VM lock?", and
    // callers act on it: Interpreter::Execute takes a guard when the answer is no,
    // which is what lets a native downcall re-enter Java. Leaving the counter at its
    // old value made that question return yes for a thread holding nothing, so the
    // re-entering call ran bytecode with no lock at all.
    t_vm_lock_depth = 0;
    for (int i = 0; i < depth_; ++i) g_vm_lock.unlock();
}

VmLockRelease::~VmLockRelease() {
    for (int i = 0; i < depth_; ++i) g_vm_lock.lock();
    t_vm_lock_depth = depth_;
}

int VmLockDepth() { return t_vm_lock_depth; }

void VmLockUnwindTo(int depth) {
    if (depth < 0) depth = 0;
    while (t_vm_lock_depth > depth) {
        --t_vm_lock_depth;
        g_vm_lock.unlock();
    }
}

namespace Monitor {

uint32_t SelfThreadId() {
    if (t_thread_id == 0) {
        static std::atomic<uint32_t> next{1};
        t_thread_id = next.fetch_add(1);
        // Record the mapping once, on the thread that owns it, so a monitor report can
        // name its holder in the same terms as every other diagnostic.
        if (t_thread_id < kMaxKuartThreads) {
            g_os_tid_by_kuart_id[t_thread_id].store(os_thread_id(), std::memory_order_relaxed);
        }
    }
    return t_thread_id;
}

void Enter(DexObject* obj) {
    if (obj == nullptr) return;
    const uint32_t self = SelfThreadId();

    {
        std::unique_lock<std::mutex> lock(g_monitor_mutex);
        if (obj->lock_owner_tid == self) {
            ++obj->lock_count;
            return;
        }
        if (obj->lock_owner_tid == 0) {
            obj->lock_owner_tid = self;
            obj->lock_count = 1;
            return;
        }
    }

    // Contended: the owner can only release it by running bytecode, which needs
    // the VM lock this thread is holding.
    //
    // Tracked, because this is the shape of hang that is hardest to see from outside:
    // the thread is parked on a condition variable inside KuART, so no bionic shim is
    // involved and nothing else logs it. WaitKind::kJavaMonitor existed for this and
    // was never constructed — it named a case the registry could not actually observe.
    // owner= carries the monitor's holder, which is the whole question for a monitor.
    VmLockRelease unlocked;
    const BlockingWaitScope tracked(WaitKind::kJavaMonitor, obj, guest_return_address(6));
    std::unique_lock<std::mutex> lock(g_monitor_mutex);
    blocking_wait_note_owner(os_tid_for(obj->lock_owner_tid));
    g_monitor_cv.wait(lock, [obj] { return obj->lock_owner_tid == 0; });
    obj->lock_owner_tid = self;
    obj->lock_count = 1;
}

bool Exit(DexObject* obj) {
    if (obj == nullptr) return false;
    const uint32_t self = SelfThreadId();

    std::unique_lock<std::mutex> lock(g_monitor_mutex);
    if (obj->lock_owner_tid != self || obj->lock_count == 0) return false;
    if (--obj->lock_count == 0) {
        obj->lock_owner_tid = 0;
        lock.unlock();
        g_monitor_cv.notify_all();
    }
    return true;
}

bool Wait(DexObject* obj, int64_t millis, int32_t nanos) {
    if (obj == nullptr) return false;
    const uint32_t self = SelfThreadId();

    uint32_t saved_count;
    {
        std::unique_lock<std::mutex> lock(g_monitor_mutex);
        if (obj->lock_owner_tid != self || obj->lock_count == 0) return false;
        // wait() releases the monitor completely, however deep the recursion is,
        // and restores that same depth on return.
        saved_count = obj->lock_count;
        obj->lock_owner_tid = 0;
        obj->lock_count = 0;
    }
    g_monitor_cv.notify_all();

    {
        VmLockRelease unlocked;
        // Object.wait: parked until notified, or until the timeout if one was given.
        // The timeout becomes the budget, so an app that asked to wait a minute is not
        // reported as stalled three seconds in — and one that asked to wait forever is.
        const BlockingWaitScope tracked(WaitKind::kJavaMonitor, obj, guest_return_address(6));
        if (millis > 0 || nanos > 0) {
            blocking_wait_note_budget(static_cast<uint64_t>(millis) +
                                      static_cast<uint64_t>(nanos) / 1000000ull);
        }
        std::unique_lock<std::mutex> lock(g_monitor_mutex);

        // Wake when someone notifies (notify_seq changes) or the monitor is free
        // and the deadline passed. A spurious wakeup just re-checks.
        const uint32_t seq_at_entry = obj->notify_seq;
        auto notified = [obj, seq_at_entry] { return obj->notify_seq != seq_at_entry; };
        if (millis <= 0 && nanos <= 0) {
            g_monitor_cv.wait(lock, notified);
        } else {
            const auto timeout = std::chrono::milliseconds(millis) +
                                 std::chrono::nanoseconds(nanos);
            g_monitor_cv.wait_for(lock, timeout, notified);
        }

        g_monitor_cv.wait(lock, [obj] { return obj->lock_owner_tid == 0; });
        obj->lock_owner_tid = self;
        obj->lock_count = saved_count;
    }
    return true;
}

bool Notify(DexObject* obj, bool /*all*/) {
    if (obj == nullptr) return false;
    const uint32_t self = SelfThreadId();

    {
        std::unique_lock<std::mutex> lock(g_monitor_mutex);
        if (obj->lock_owner_tid != self || obj->lock_count == 0) return false;
        // One counter serves both notify and notifyAll: waiters re-check their
        // own condition anyway, so waking all of them is always correct.
        ++obj->notify_seq;
    }
    g_monitor_cv.notify_all();
    return true;
}

}  // namespace Monitor

}  // namespace kuart
}  // namespace kudroid
