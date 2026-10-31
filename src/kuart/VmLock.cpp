#include "kudroid/kuart/VmLock.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <pthread.h>
#include <thread>
#include <unordered_map>
#if !defined(__APPLE__)
#include <sys/syscall.h>
#include <unistd.h>
#endif

#include "kudroid/abi/BlockingWaitRegistry.h"
#include "kudroid/kuart/DexObject.h"

namespace kudroid {
namespace kuart {

namespace {

// Recursive ownership with FIFO handoff to one waiting thread.
class FairRecursiveMutex {
public:
    void lock() {
        std::unique_lock<std::mutex> lock(m_);
        const std::thread::id self = std::this_thread::get_id();
        if (count_ == 0 && waiters_.empty()) {
            owner_ = self;
            count_ = 1;
            return;
        }
        if (count_ > 0 && owner_ == self) {
            ++count_;
            return;
        }
        Waiter waiter;
        waiters_.push_back(&waiter);
        waiter.cv.wait(lock, [this, &waiter] {
            return count_ == 0 && !waiters_.empty() && waiters_.front() == &waiter;
        });
        waiters_.pop_front();
        owner_ = self;
        count_ = 1;
    }

    void unlock() {
        std::lock_guard<std::mutex> lock(m_);
        if (count_ == 0 || owner_ != std::this_thread::get_id()) return;
        if (--count_ == 0) {
            owner_ = std::thread::id{};
            if (!waiters_.empty()) waiters_.front()->cv.notify_one();
        }
    }

private:
    std::mutex m_;
    struct Waiter { std::condition_variable cv; };
    std::thread::id owner_{};
    int count_ = 0;
    std::deque<Waiter*> waiters_;
};

FairRecursiveMutex g_vm_lock;

// Recursion depth; main thread enters bytecode without VmLockGuard.
thread_local int t_vm_lock_depth = 0;

// One mutex for all monitors; VM lock already serializes bytecode.
std::mutex g_monitor_mutex;
struct WaitTicket {
    std::condition_variable cv;
    bool notified = false;
    DexObject* thread = nullptr;
};
struct MonitorState {
    std::condition_variable entry_cv;
    size_t users = 0;
    std::deque<WaitTicket*> waits;
};
std::unordered_map<DexObject*, std::shared_ptr<MonitorState>> g_monitor_states;

std::shared_ptr<MonitorState> monitor_state(DexObject* obj) {
    auto& state = g_monitor_states[obj];
    if (!state) state = std::make_shared<MonitorState>();
    return state;
}

void release_monitor_state(DexObject* obj, const std::shared_ptr<MonitorState>& state) {
    if (--state->users == 0) g_monitor_states.erase(obj);
}

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

// Threads currently parked in a monitor Enter/Wait. kuart_shutdown waits these
// out (bounded): deleting the runtime heap under a parked waiter that re-touches
// the object on wake is a use-after-free. Defined here (not in the anonymous
// namespace above) to match the header declaration.
std::atomic<int> g_monitorWaiters{0};

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

    std::shared_ptr<MonitorState> state;
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
        state = monitor_state(obj);
        ++state->users;
        ++g_monitorWaiters;
    }

    // Contended: owner releases it only by running bytecode.
    // Tracked since parked waits here are otherwise invisible.
    {
        VmLockRelease unlocked;
        const BlockingWaitScope tracked(WaitKind::kJavaMonitor, obj, guest_return_address(6));
        std::unique_lock<std::mutex> lock(g_monitor_mutex);
        blocking_wait_note_owner(os_tid_for(obj->lock_owner_tid));
        state->entry_cv.wait(lock, [obj] { return obj->lock_owner_tid == 0; });
        obj->lock_owner_tid = self;
        obj->lock_count = 1;
        release_monitor_state(obj, state);
    }
    --g_monitorWaiters;
}

bool Exit(DexObject* obj) {
    if (obj == nullptr) return false;
    const uint32_t self = SelfThreadId();

    std::unique_lock<std::mutex> lock(g_monitor_mutex);
    if (obj->lock_owner_tid != self || obj->lock_count == 0) return false;
    if (--obj->lock_count == 0) {
        obj->lock_owner_tid = 0;
        auto state = g_monitor_states.find(obj);
        if (state != g_monitor_states.end()) state->second->entry_cv.notify_one();
    }
    return true;
}

bool IsOwned(DexObject* obj) {
    if (obj == nullptr) return false;
    std::lock_guard<std::mutex> lock(g_monitor_mutex);
    return obj->lock_owner_tid == SelfThreadId() && obj->lock_count != 0;
}

bool Wait(DexObject* obj, int64_t millis, int32_t nanos, DexObject* thread) {
    if (obj == nullptr) return false;
    const uint32_t self = SelfThreadId();

    uint32_t saved_count;
    WaitTicket ticket;
    ticket.thread = thread;
    std::shared_ptr<MonitorState> state;
    {
        std::unique_lock<std::mutex> lock(g_monitor_mutex);
        if (obj->lock_owner_tid != self || obj->lock_count == 0) return false;
        // wait() releases the monitor fully and restores depth on return.
        saved_count = obj->lock_count;
        state = monitor_state(obj);
        ++state->users;
        ++g_monitorWaiters;
        // Register before releasing ownership so notifications cannot be lost.
        state->waits.push_back(&ticket);
        obj->lock_owner_tid = 0;
        obj->lock_count = 0;
        state->entry_cv.notify_one();
    }

    {
        VmLockRelease unlocked;
        // Parked wait with timeout as stall budget; kJavaWait, not kJavaMonitor.
        const BlockingWaitScope tracked(WaitKind::kJavaWait, obj, guest_return_address(6));
        if (millis > 0 || nanos > 0) {
            blocking_wait_note_budget(static_cast<uint64_t>(millis) +
                                      static_cast<uint64_t>(nanos) / 1000000ull);
        }
        std::unique_lock<std::mutex> lock(g_monitor_mutex);

        auto notified = [&ticket] { return ticket.notified; };
        if (millis <= 0 && nanos <= 0) {
            ticket.cv.wait(lock, notified);
        } else {
            const auto now = std::chrono::steady_clock::now();
            const auto maximum = std::chrono::steady_clock::time_point::max();
            const auto remaining = maximum - now;
            const int64_t max_millis =
                std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count();
            auto deadline = maximum;
            if (millis < max_millis) {
                const auto timeout = std::chrono::milliseconds(millis) +
                                     std::chrono::nanoseconds(nanos);
                if (timeout < remaining) deadline = now + timeout;
            }
            ticket.cv.wait_until(lock, deadline, notified);
        }
        for (auto it = state->waits.begin(); it != state->waits.end(); ++it) {
            if (*it == &ticket) { state->waits.erase(it); break; }
        }
        state->entry_cv.wait(lock, [obj] { return obj->lock_owner_tid == 0; });
        obj->lock_owner_tid = self;
        obj->lock_count = saved_count;
        release_monitor_state(obj, state);
    }
    --g_monitorWaiters;
    return true;
}

void Interrupt(DexObject* thread) {
    if (thread == nullptr) return;
    std::lock_guard<std::mutex> lock(g_monitor_mutex);
    for (const auto& entry : g_monitor_states) {
        for (WaitTicket* ticket : entry.second->waits) {
            if (ticket->thread == thread) {
                ticket->notified = true;
                ticket->cv.notify_one();
            }
        }
    }
}

bool Notify(DexObject* obj, bool all) {
    if (obj == nullptr) return false;
    const uint32_t self = SelfThreadId();

    {
        std::unique_lock<std::mutex> lock(g_monitor_mutex);
        if (obj->lock_owner_tid != self || obj->lock_count == 0) return false;
        ++obj->notify_seq;
        auto state = g_monitor_states.find(obj);
        if (state != g_monitor_states.end()) {
            for (WaitTicket* ticket : state->second->waits) {
                if (ticket->notified) continue;
                ticket->notified = true;
                ticket->cv.notify_one();
                if (!all) break;
            }
        }
    }
    return true;
}

}  // namespace Monitor

}  // namespace kuart
}  // namespace kudroid
