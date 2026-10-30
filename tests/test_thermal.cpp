#include "kudroid/platform/ThermalShim.h"
#include "kudroid/platform/GraphicsShim.h"
#include "kudroid/kuart/VmLock.h"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>
#include "kudroid/KuArtRuntime.h"

using namespace std::chrono_literals;

namespace {
int checks = 0;
int failures = 0;

void Check(bool ok, const char* description) {
    ++checks;
    failures += !ok;
    std::printf("%s %s\n", ok ? "OK" : "FAIL", description);
}

struct Capture {
    std::mutex mutex;
    std::condition_variable cv;
    std::vector<int> statuses;
    std::thread::id thread;
};

void Record(void* data, int status) {
    auto& capture = *static_cast<Capture*>(data);
    std::lock_guard<std::mutex> lock(capture.mutex);
    capture.statuses.push_back(status);
    capture.thread = std::this_thread::get_id();
    capture.cv.notify_all();
}

bool Wait(Capture& capture, size_t count) {
    std::unique_lock<std::mutex> lock(capture.mutex);
    return capture.cv.wait_for(lock, 2s, [&] { return capture.statuses.size() >= count; });
}

void Drain() {
    Capture barrier;
    void* manager = bionic_AThermal_acquireManager();
    Check(manager && bionic_AThermal_registerThermalStatusListener(manager, Record, &barrier) == 0,
          "worker barrier registers");
    Check(Wait(barrier, 1), "worker barrier completes");
    bionic_AThermal_releaseManager(manager);
}

void* Symbol(const char* name) {
    size_t count = 0;
    const auto* symbols = kudroid::get_graphics_symbols(&count);
    for (size_t i = 0; i < count; ++i) {
        if (std::strcmp(symbols[i].name, name) == 0) return symbols[i].address;
    }
    return nullptr;
}

void StatusAndBindings() {
    kudroid_thermal_reset();
    kudroid_set_thermal_status(-1);
    Check(kudroid_get_thermal_status() == -1, "unavailable host status remains unknown");
    Check(bionic_AThermal_acquireManager() == nullptr, "unavailable host cannot supply a thermal manager");
    Check(bionic_AThermal_getCurrentThermalStatus(nullptr) == -1, "invalid manager returns the native error status");
    Check(bionic_AThermal_registerThermalStatusListener(nullptr, Record, nullptr) == EINVAL,
          "registration rejects an invalid manager");
    Check(bionic_AThermal_unregisterThermalStatusListener(nullptr, Record, nullptr) == EINVAL,
          "unregistration rejects an invalid manager");
    Check(Symbol("AThermal_acquireManager") == reinterpret_cast<void*>(&bionic_AThermal_acquireManager)
          && Symbol("AThermal_releaseManager") == reinterpret_cast<void*>(&bionic_AThermal_releaseManager)
          && Symbol("AThermal_getCurrentThermalStatus") == reinterpret_cast<void*>(&bionic_AThermal_getCurrentThermalStatus)
          && Symbol("AThermal_registerThermalStatusListener") == reinterpret_cast<void*>(&bionic_AThermal_registerThermalStatusListener)
          && Symbol("AThermal_unregisterThermalStatusListener") == reinterpret_cast<void*>(&bionic_AThermal_unregisterThermalStatusListener),
          "native thermal entry points resolve to their actual bindings");
    auto headroom = reinterpret_cast<float(*)(void*, int)>(Symbol("AThermal_getThermalHeadroom"));
    Check(headroom && std::isnan(headroom(nullptr, 0)), "unsupported headroom returns NaN through the float ABI");

    kudroid_set_thermal_status(0);
    void* manager = bionic_AThermal_acquireManager();
    void* otherManager = bionic_AThermal_acquireManager();
    Check(manager && otherManager && manager != otherManager, "acquisitions return independent handles");
    if (!manager || !otherManager) return;
    Check(bionic_AThermal_getCurrentThermalStatus(manager) == 0, "manager queries the actual host status");
    Check(bionic_AThermal_registerThermalStatusListener(manager, nullptr, nullptr) == EINVAL,
          "registration rejects a null callback");
    Capture capture;
    Check(bionic_AThermal_registerThermalStatusListener(manager, Record, &capture) == 0,
          "valid listener registers");
    Check(bionic_AThermal_registerThermalStatusListener(manager, Record, &capture) == EINVAL,
          "duplicate callback and data registration returns EINVAL");
    kudroid_set_thermal_status(0);
    kudroid_set_thermal_status(1);
    kudroid_set_thermal_status(3);
    kudroid_set_thermal_status(4);
    Check(Wait(capture, 4), "initial status and all updates reach the listener");
    {
        std::lock_guard<std::mutex> lock(capture.mutex);
        Check(capture.statuses == std::vector<int>({0, 1, 3, 4}), "callbacks preserve status order without duplicate unchanged updates");
        Check(capture.thread != std::this_thread::get_id(), "callbacks run asynchronously on a worker");
    }
    kudroid_set_thermal_status(7);
    kudroid_set_thermal_status(-2);
    Check(bionic_AThermal_getCurrentThermalStatus(otherManager) == 4, "invalid host status leaves every manager intact");
    Capture distinct;
    Check(bionic_AThermal_registerThermalStatusListener(manager, Record, &distinct) == 0,
          "the same callback can register with different data");
    Check(Wait(distinct, 1), "new listener receives the current status");
    Check(bionic_AThermal_unregisterThermalStatusListener(manager, Record, &distinct) == 0,
          "distinct listener unregisters independently");
    Check(bionic_AThermal_unregisterThermalStatusListener(manager, Record, &capture) == 0,
          "original listener unregisters");
    Check(bionic_AThermal_unregisterThermalStatusListener(manager, Record, &capture) == EINVAL,
          "absent listener unregistration returns EINVAL");
    kudroid_set_thermal_status(0);
    Drain();
    {
        std::lock_guard<std::mutex> lock(capture.mutex);
        Check(capture.statuses.size() == 4, "unregistered listener receives no subsequent callback");
    }
    bionic_AThermal_releaseManager(manager);
    Check(bionic_AThermal_getCurrentThermalStatus(manager) == -1, "released handle becomes invalid");
    bionic_AThermal_releaseManager(otherManager);
}

struct Blocking {
    std::mutex mutex;
    std::condition_variable cv;
    bool entered = false;
    bool proceed = false;
    bool timedOut = false;
    bool resetInside = false;
    int calls = 0;
};

void Block(void* data, int) {
    auto& capture = *static_cast<Blocking*>(data);
    {
        std::unique_lock<std::mutex> lock(capture.mutex);
        ++capture.calls;
        capture.entered = true;
        capture.cv.notify_all();
        capture.timedOut = !capture.cv.wait_for(lock, 5s, [&] { return capture.proceed; });
    }
    kudroid::kuart::VmLockGuard guard;
    if (capture.resetInside) kudroid_thermal_reset();
}

bool WaitEntered(Blocking& capture) {
    std::unique_lock<std::mutex> lock(capture.mutex);
    return capture.cv.wait_for(lock, 2s, [&] { return capture.entered; });
}

void Proceed(Blocking& capture) {
    std::lock_guard<std::mutex> lock(capture.mutex);
    capture.proceed = true;
    capture.cv.notify_all();
}

void Retirement(int operation) {
    kudroid_thermal_reset();
    kudroid_set_thermal_status(0);
    Blocking capture;
    capture.resetInside = operation == 2;
    void* manager = bionic_AThermal_acquireManager();
    Check(manager && bionic_AThermal_registerThermalStatusListener(manager, Block, &capture) == 0,
          "blocking listener registers");
    Check(WaitEntered(capture), "blocking callback starts");
    kudroid_set_thermal_status(1);
    std::atomic<bool> started{false};
    std::mutex doneMutex;
    std::condition_variable doneCv;
    bool done = false;
    int result = -1, restoredDepth = 0;
    std::thread retire([&] {
        kudroid::kuart::VmLockGuard guard;
        started.store(true);
        if (operation == 0) result = bionic_AThermal_unregisterThermalStatusListener(manager, Block, &capture);
        else if (operation == 1) { bionic_AThermal_releaseManager(manager); result = 0; }
        else if (operation == 2) { kudroid_thermal_reset(); result = 0; }
        else { kuart_shutdown(); result = 0; }
        restoredDepth = kudroid::kuart::VmLockDepth();
        std::lock_guard<std::mutex> lock(doneMutex);
        done = true;
        doneCv.notify_all();
    });
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (!started.load() && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
    Check(started.load(), "retirement starts while holding the VM lock");
    {
        std::unique_lock<std::mutex> lock(doneMutex);
        Check(!doneCv.wait_for(lock, 30ms, [&] { return done; }), "retirement waits for the active callback");
    }
    Proceed(capture);
    retire.join();
    Check(result == 0 && restoredDepth == 1, "callback retirement completes and restores VM ownership");
    Drain();
    {
        std::lock_guard<std::mutex> lock(capture.mutex);
        Check(capture.calls == 1 && !capture.timedOut, "retirement cancels queued callbacks without blocking callback VM entry");
    }
    if (operation == 0) bionic_AThermal_releaseManager(manager);
}

void ResetDuringUnregister() {
    kudroid_set_thermal_status(0);
    Blocking capture;
    capture.resetInside = true;
    void* manager = bionic_AThermal_acquireManager();
    Check(manager && bionic_AThermal_registerThermalStatusListener(manager, Block, &capture) == 0,
          "listener for overlapping cleanup registers");
    Check(WaitEntered(capture), "overlapping cleanup callback starts");
    std::atomic<int> unregistered{-1};
    std::thread unregister([&] {
        kudroid::kuart::VmLockGuard guard;
        unregistered.store(bionic_AThermal_unregisterThermalStatusListener(manager, Block, &capture));
    });
    bool replaced = false;
    const auto registrationDeadline = std::chrono::steady_clock::now() + 2s;
    while (std::chrono::steady_clock::now() < registrationDeadline) {
        if (bionic_AThermal_registerThermalStatusListener(manager, Block, &capture) == 0) {
            replaced = true;
            break;
        }
        std::this_thread::yield();
    }
    Check(replaced, "unregister removes the registration while its old callback is still active");
    if (replaced) {
        Check(bionic_AThermal_unregisterThermalStatusListener(manager, Block, &capture) == 0,
              "replacement registration retires independently of the older active callback");
    }
    std::atomic<bool> resetDone{false};
    std::thread reset([&] {
        kudroid::kuart::VmLockGuard guard;
        kudroid_thermal_reset();
        resetDone.store(true);
    });
    const auto resetDeadline = std::chrono::steady_clock::now() + 2s;
    while (bionic_AThermal_getCurrentThermalStatus(manager) >= 0
           && std::chrono::steady_clock::now() < resetDeadline) std::this_thread::yield();
    Check(bionic_AThermal_getCurrentThermalStatus(manager) == -1 && !resetDone.load(),
          "reset waits for an active callback even after another thread removed its registration");
    Proceed(capture);
    unregister.join();
    reset.join();
    Check(unregistered.load() == 0 && resetDone.load(), "overlapping cleanup and callback self-reset complete");
    Drain();
    {
        std::lock_guard<std::mutex> lock(capture.mutex);
        Check(capture.calls == 1 && !capture.timedOut, "overlapping cleanup leaves no callback alive or queued");
    }
}

struct SelfRemoval {
    void* manager = nullptr;
    bool release = false;
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
    int result = -1;
    int calls = 0;
};

void RemoveSelf(void* data, int) {
    auto& capture = *static_cast<SelfRemoval*>(data);
    int result = 0;
    if (capture.release) bionic_AThermal_releaseManager(capture.manager);
    else result = bionic_AThermal_unregisterThermalStatusListener(capture.manager, RemoveSelf, data);
    std::lock_guard<std::mutex> lock(capture.mutex);
    capture.result = result;
    ++capture.calls;
    capture.done = true;
    capture.cv.notify_all();
}

void SelfRetirement(bool release) {
    kudroid_set_thermal_status(0);
    SelfRemoval capture;
    capture.release = release;
    capture.manager = bionic_AThermal_acquireManager();
    Check(capture.manager && bionic_AThermal_registerThermalStatusListener(capture.manager, RemoveSelf, &capture) == 0,
          "self-removing listener registers");
    {
        std::unique_lock<std::mutex> lock(capture.mutex);
        Check(capture.cv.wait_for(lock, 2s, [&] { return capture.done; }) && capture.result == 0,
              "callback can unregister or release its own manager without deadlocking");
    }
    kudroid_set_thermal_status(1);
    Drain();
    {
        std::lock_guard<std::mutex> lock(capture.mutex);
        Check(capture.calls == 1, "self-retired callback is not invoked again");
    }
    if (!release) bionic_AThermal_releaseManager(capture.manager);
}
}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    StatusAndBindings();
    for (int operation : {0, 1, 2, 3}) Retirement(operation);
    ResetDuringUnregister();
    SelfRetirement(false);
    SelfRetirement(true);
    kudroid_thermal_reset();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures != 0;
}
