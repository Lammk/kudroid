#include "kudroid/kuart/DexObject.h"
#include "kudroid/kuart/VmLock.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

using namespace kudroid::kuart;
static int failures;
static void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    failures += !ok;
}
static void awaitParked(int count) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (g_monitorWaiters.load() != count && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if (g_monitorWaiters.load() != count) std::abort();
}
int main() {
    DexObject object;
    check(!Monitor::Exit(&object) && !Monitor::Wait(&object, 1, 0)
              && !Monitor::Notify(&object, false), "unowned monitor operations rejected");
    std::atomic<int> completed{0};
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; i++) threads.emplace_back([&] {
        VmLockGuard guard;
        Monitor::Enter(&object);
        Monitor::Enter(&object);
        Monitor::Wait(&object, 0, 0);
        if (object.lock_count != 2 || VmLockDepth() != 1) std::abort();
        completed.fetch_add(1);
        Monitor::Exit(&object);
        Monitor::Exit(&object);
    });
    awaitParked(4);
    {
        VmLockGuard guard;
        Monitor::Enter(&object);
        check(Monitor::Notify(&object, false), "notify accepted by owner");
        check(completed.load() == 0, "notification does not bypass monitor ownership");
        Monitor::Exit(&object);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    check(completed.load() == 1, "notify selects exactly one waiter");
    {
        VmLockGuard guard;
        Monitor::Enter(&object);
        Monitor::Notify(&object, false);
        Monitor::Notify(&object, false);
        Monitor::Exit(&object);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    check(completed.load() == 3, "consecutive notifications select distinct waiting threads");
    {
        VmLockGuard guard;
        Monitor::Enter(&object);
        Monitor::Notify(&object, true);
        Monitor::Exit(&object);
    }
    for (auto& thread : threads) thread.join();
    check(completed.load() == 4 && g_monitorWaiters.load() == 0,
          "notifyAll wakes remaining waiters and restores recursive ownership");
    {
        VmLockGuard guard;
        Monitor::Enter(&object);
        const auto start = std::chrono::steady_clock::now();
        check(Monitor::Wait(&object, 20, 0), "timed wait completes");
        check(std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(20),
              "timed wait honors deadline");
        check(object.lock_count == 1 && VmLockDepth() == 1, "timeout restores monitor and VM lock");
        Monitor::Exit(&object);
    }
    for (int i = 0; i < 100; i++) {
        std::atomic<bool> entered{false};
        std::thread waiter([&] {
            VmLockGuard guard;
            Monitor::Enter(&object);
            entered.store(true);
            Monitor::Wait(&object, 0, 0);
            Monitor::Exit(&object);
        });
        while (!entered.load()) std::this_thread::yield();
        {
            VmLockGuard guard;
            Monitor::Enter(&object);
            Monitor::Notify(&object, false);
            Monitor::Exit(&object);
        }
        waiter.join();
    }
    check(g_monitorWaiters.load() == 0, "notifications during wait release are not lost");
    return failures ? 1 : 0;
}
