// Java queues and native descriptor callbacks share the owning thread's looper.

#include <algorithm>
#include <climits>
#include <cstdint>
#include <mutex>
#include <new>

extern "C" void* bionic_ALooper_prepare(int);
extern "C" void bionic_ALooper_acquire(void*);
extern "C" void bionic_ALooper_release(void*);
extern "C" void bionic_ALooper_wake(void*);
extern "C" int bionic_ALooper_pollOnce(int, int*, int*, void**);

struct LooperWaitSlot {
    void* looper = nullptr;
};
static std::mutex g_mainLooperMutex;
static LooperWaitSlot* g_mainLooperSlot = nullptr;

extern "C" int64_t kudroid_looper_init(void) {
    auto* slot = new (std::nothrow) LooperWaitSlot();
    if (slot == nullptr) return 0;
    slot->looper = bionic_ALooper_prepare(1);
    if (slot->looper == nullptr) {
        delete slot;
        return 0;
    }
    bionic_ALooper_acquire(slot->looper);
    return reinterpret_cast<int64_t>(slot);
}

extern "C" void kudroid_looper_poll(int64_t ptr, int64_t timeout_millis) {
    auto* slot = reinterpret_cast<LooperWaitSlot*>(ptr);
    if (slot == nullptr) return;
    const int timeout = timeout_millis < 0 ? -1 :
        static_cast<int>(std::min<int64_t>(timeout_millis, INT_MAX));
    bionic_ALooper_pollOnce(timeout, nullptr, nullptr, nullptr);
}

extern "C" void kudroid_looper_wake(int64_t ptr) {
    auto* slot = reinterpret_cast<LooperWaitSlot*>(ptr);
    if (slot == nullptr) return;
    bionic_ALooper_wake(slot->looper);
}

extern "C" void kudroid_looper_set_main(int64_t ptr) {
    std::lock_guard<std::mutex> lock(g_mainLooperMutex);
    g_mainLooperSlot = reinterpret_cast<LooperWaitSlot*>(ptr);
}

extern "C" void kudroid_looper_reset_main(void) {
    std::lock_guard<std::mutex> lock(g_mainLooperMutex);
    g_mainLooperSlot = nullptr;
}

extern "C" void kudroid_looper_wake_main(void) {
    // Keep the slot protected until its wake has finished. destroy() clears the
    // pointer under this mutex before freeing it, so input cannot wake freed memory.
    std::lock_guard<std::mutex> lock(g_mainLooperMutex);
    kudroid_looper_wake(reinterpret_cast<int64_t>(g_mainLooperSlot));
}

extern "C" int kudroid_looper_is_main(int64_t ptr) {
    std::lock_guard<std::mutex> lock(g_mainLooperMutex);
    return reinterpret_cast<LooperWaitSlot*>(ptr) == g_mainLooperSlot;
}

extern "C" void kudroid_looper_destroy(int64_t ptr) {
    auto* slot = reinterpret_cast<LooperWaitSlot*>(ptr);
    if (slot == nullptr) return;
    {
        std::lock_guard<std::mutex> lock(g_mainLooperMutex);
        if (g_mainLooperSlot == slot) g_mainLooperSlot = nullptr;
    }
    bionic_ALooper_release(slot->looper);
    delete slot;
}
