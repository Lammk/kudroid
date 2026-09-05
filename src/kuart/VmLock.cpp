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

// Recursion depth; main thread enters bytecode without VmLockGuard.
thread_local int t_vm_lock_depth = 0;

// One mutex for all monitors; VM lock already serializes bytecode.
std::mutex g_monitor_mutex;
std::condition_variable g_monitor_cv;

thread_local uint32_t t_thread_id = 0;

// KuART id to OS tid mapping for diagnostics.
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

// OS tid for KuART id, or 0 when unknown.
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
    // Reset depth while released so re-entry retakes the lock.
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
        // Record mapping so monitor reports use OS tids.
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

    // Contended: owner releases it only by running bytecode.
    // Tracked since parked waits here are otherwise invisible.
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
        // wait() releases the monitor fully and restores depth on return.
        saved_count = obj->lock_count;
        obj->lock_owner_tid = 0;
        obj->lock_count = 0;
    }
    g_monitor_cv.notify_all();

    {
        VmLockRelease unlocked;
        // Parked wait with timeout as stall budget; kJavaWait, not kJavaMonitor.
        const BlockingWaitScope tracked(WaitKind::kJavaWait, obj, guest_return_address(6));
        if (millis > 0 || nanos > 0) {
            blocking_wait_note_budget(static_cast<uint64_t>(millis) +
                                      static_cast<uint64_t>(nanos) / 1000000ull);
        }
        std::unique_lock<std::mutex> lock(g_monitor_mutex);

        // Wake on notify or free monitor; spurious wakeups re-check.
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
        // One counter for notify/notifyAll; waiters re-check anyway.
        ++obj->notify_seq;
    }
    g_monitor_cv.notify_all();
    return true;
}

}  // namespace Monitor

}  // namespace kuart
}  // namespace kudroid
