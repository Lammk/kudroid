#include "kudroid/kuart/VmLock.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>

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
    for (int i = 0; i < depth_; ++i) g_vm_lock.unlock();
}

VmLockRelease::~VmLockRelease() {
    for (int i = 0; i < depth_; ++i) g_vm_lock.lock();
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
    VmLockRelease unlocked;
    std::unique_lock<std::mutex> lock(g_monitor_mutex);
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
