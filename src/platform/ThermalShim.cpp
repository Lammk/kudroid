#include "kudroid/platform/ThermalShim.h"
#include "kudroid/BionicShim.h"
#include "kudroid/kuart/VmLock.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <condition_variable>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <vector>

namespace {
struct Listener {
    kudroid_thermal_callback callback;
    void* data;
    bool active = true;
    bool running = false;
    Listener(kudroid_thermal_callback callback, void* data) : callback(callback), data(data) {}
};

struct Manager {
    std::vector<std::shared_ptr<Listener>> listeners;
};

struct Notification {
    std::shared_ptr<Listener> listener;
    int status;
};

struct ThermalState {
    std::mutex mutex;
    std::condition_variable cv;
    std::unordered_map<void*, std::unique_ptr<Manager>> managers;
    std::atomic<int> status{-1};
    std::deque<Notification> queue;
    std::shared_ptr<Listener> executing;
    bool resetting = false;
    std::once_flag worker;
};

ThermalState& State() {
    // The worker and its state live for the process lifetime.
    static auto* state = new ThermalState;
    return *state;
}

thread_local Listener* executingListener = nullptr;

void Worker() {
    // Native callbacks require the guest thread's TLS and floating-point state.
    bionic_init_main_thread_tls();
    auto& state = State();
    for (;;) {
        Notification notification;
        {
            std::unique_lock<std::mutex> lock(state.mutex);
            state.cv.wait(lock, [&] { return !state.queue.empty(); });
            notification = std::move(state.queue.front());
            state.queue.pop_front();
            if (!notification.listener->active) continue;
            notification.listener->running = true;
            state.executing = notification.listener;
        }
        auto& listener = *notification.listener;
        executingListener = &listener;
        listener.callback(listener.data, notification.status);
        executingListener = nullptr;
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            listener.running = false;
            state.executing.reset();
        }
        state.cv.notify_all();
    }
}

// Queue under the state lock to preserve registration and status-change order.
void EnqueueLocked(const std::shared_ptr<Listener>& listener, int status) {
    auto& state = State();
    state.queue.push_back(Notification{listener, status});
    state.cv.notify_all();
}

void RetireLocked(const std::shared_ptr<Listener>& listener) {
    auto& state = State();
    listener->active = false;
    state.queue.erase(std::remove_if(state.queue.begin(), state.queue.end(), [&](const auto& value) {
        return value.listener == listener;
    }), state.queue.end());
}

void WaitForListener(const std::shared_ptr<Listener>& listener) {
    if (!listener || executingListener == listener.get()) return;
    // A callback may need the VM lock before it can complete.
    kudroid::kuart::VmLockRelease unlocked;
    auto& state = State();
    std::unique_lock<std::mutex> lock(state.mutex);
    state.cv.wait(lock, [&] { return !listener->running; });
}
}  // namespace

extern "C" void kudroid_set_thermal_status(int status) {
    if (status < -1 || status > 6) return;
    auto& state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (state.status.exchange(status, std::memory_order_relaxed) == status) return;
    for (const auto& entry : state.managers) {
        for (const auto& listener : entry.second->listeners) {
            try {
                EnqueueLocked(listener, status);
            } catch (const std::bad_alloc&) {
                // Polling still reports the current status if notification allocation fails.
            }
        }
    }
}

extern "C" int kudroid_get_thermal_status(void) {
    return State().status.load(std::memory_order_relaxed);
}

extern "C" void* bionic_AThermal_acquireManager(void) {
    auto& state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (state.resetting || state.status.load(std::memory_order_relaxed) < 0) return nullptr;
    try {
        auto manager = std::make_unique<Manager>();
        void* handle = manager.get();
        state.managers.emplace(handle, std::move(manager));
        return handle;
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}

extern "C" void bionic_AThermal_releaseManager(void* manager) {
    auto& state = State();
    std::unique_ptr<Manager> released;
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        auto found = state.managers.find(manager);
        if (found == state.managers.end()) return;
        released = std::move(found->second);
        state.managers.erase(found);
        for (const auto& listener : released->listeners) RetireLocked(listener);
    }
    for (const auto& listener : released->listeners) WaitForListener(listener);
}

extern "C" int bionic_AThermal_getCurrentThermalStatus(void* manager) {
    auto& state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    return state.managers.count(manager) ? state.status.load(std::memory_order_relaxed) : -1;
}

extern "C" float bionic_AThermal_getThermalHeadroom(void*, int) {
    // The host reports severity, not temperature forecasts.
    return std::numeric_limits<float>::quiet_NaN();
}

extern "C" int bionic_AThermal_registerThermalStatusListener(
        void* manager, kudroid_thermal_callback callback, void* data) {
    auto& state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    auto found = state.managers.find(manager);
    if (found == state.managers.end() || callback == nullptr) return EINVAL;
    if (state.resetting) return EPIPE;
    auto& listeners = found->second->listeners;
    for (const auto& listener : listeners) {
        if (listener->callback == callback && listener->data == data) return EINVAL;
    }
    try {
        std::call_once(state.worker, [] { std::thread(Worker).detach(); });
        auto listener = std::make_shared<Listener>(callback, data);
        listeners.push_back(listener);
        try {
            EnqueueLocked(listener, state.status.load(std::memory_order_relaxed));
        } catch (const std::bad_alloc&) {
            listeners.pop_back();
            return EPIPE;
        }
    } catch (const std::bad_alloc&) {
        return EPIPE;
    } catch (const std::system_error&) {
        return EPIPE;
    }
    return 0;
}

extern "C" int bionic_AThermal_unregisterThermalStatusListener(
        void* manager, kudroid_thermal_callback callback, void* data) {
    auto& state = State();
    std::shared_ptr<Listener> removed;
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        auto found = state.managers.find(manager);
        if (found == state.managers.end()) return EINVAL;
        auto& listeners = found->second->listeners;
        auto listener = std::find_if(listeners.begin(), listeners.end(), [&](const auto& value) {
            return value->callback == callback && value->data == data;
        });
        if (listener == listeners.end()) return EINVAL;
        removed = *listener;
        listeners.erase(listener);
        RetireLocked(removed);
    }
    WaitForListener(removed);
    return 0;
}

extern "C" void kudroid_thermal_reset(void) {
    auto& state = State();
    std::unordered_map<void*, std::unique_ptr<Manager>> released;
    std::shared_ptr<Listener> running;
    bool alreadyResetting = false;
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        alreadyResetting = state.resetting;
        if (!alreadyResetting) {
            state.resetting = true;
            released.swap(state.managers);
            for (const auto& entry : released) {
                for (const auto& listener : entry.second->listeners) listener->active = false;
            }
            state.queue.clear();
            running = state.executing;
        }
    }
    if (alreadyResetting) {
        if (executingListener) return;
        kudroid::kuart::VmLockRelease unlocked;
        std::unique_lock<std::mutex> lock(state.mutex);
        state.cv.wait(lock, [&] { return !state.resetting; });
        return;
    }
    WaitForListener(running);
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.resetting = false;
    }
    state.cv.notify_all();
}
