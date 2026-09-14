// Native wait primitive behind android.os.MessageQueue, plus the touch wake.
//
// Kept in its own translation unit on purpose: this code needs
// <condition_variable>, whose include chain reaches <wchar.h> and leaves FILE
// only forward-declared. If a file that also pulls in jni.h (-> <stdio.h>)
// includes <condition_variable> first, libstdc++/glibc 13 compiles
// <bits/stdio.h> against an incomplete FILE and the build fails. Nothing here
// includes jni.h, so the two never mix.
//
// AOSP's MessageQueue blocks in native epoll and registers the window's input
// channel fd as a second wake source, so input is delivered to the Looper
// thread by native code (InputEventReceiver.dispatchInputEvent). KuDroid's Java
// MessageQueue used to block in Object.wait(), which can only be woken by a
// Java notify — an interpreted call that needs the global VM lock. Touch
// delivery therefore needed a second thread holding that lock, serializing
// touch against the renderer. This slot is the native condition the queue waits
// on: a message enqueue and a touch injection both set `pending` and signal it,
// so only the Looper thread ever runs Java for touch.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <new>

struct LooperWaitSlot {
    std::mutex mtx;
    std::condition_variable cv;
    bool pending = false;
};
static std::atomic<LooperWaitSlot*> g_mainLooperSlot{nullptr};

extern "C" int64_t kudroid_looper_init(void) {
    return reinterpret_cast<int64_t>(new (std::nothrow) LooperWaitSlot());
}

extern "C" void kudroid_looper_poll(int64_t ptr, int64_t timeout_millis) {
    auto* slot = reinterpret_cast<LooperWaitSlot*>(ptr);
    if (slot == nullptr) return;
    std::unique_lock<std::mutex> lock(slot->mtx);
    // Consume an already-set wake first: an enqueue or touch that landed before
    // this wait must not be slept through.
    if (slot->pending) {
        slot->pending = false;
        return;
    }
    if (timeout_millis < 0) {
        slot->cv.wait(lock, [slot] { return slot->pending; });
    } else if (timeout_millis > 0) {
        slot->cv.wait_for(lock, std::chrono::milliseconds(timeout_millis),
                          [slot] { return slot->pending; });
    }
    slot->pending = false;
}

extern "C" void kudroid_looper_wake(int64_t ptr) {
    auto* slot = reinterpret_cast<LooperWaitSlot*>(ptr);
    if (slot == nullptr) return;
    {
        std::lock_guard<std::mutex> lock(slot->mtx);
        slot->pending = true;
    }
    slot->cv.notify_all();
}

extern "C" void kudroid_looper_set_main(int64_t ptr) {
    g_mainLooperSlot.store(reinterpret_cast<LooperWaitSlot*>(ptr), std::memory_order_release);
}

extern "C" void kudroid_looper_reset_main(void) {
    g_mainLooperSlot.store(nullptr, std::memory_order_release);
}

extern "C" void kudroid_looper_wake_main(void) {
    kudroid_looper_wake(
        reinterpret_cast<int64_t>(g_mainLooperSlot.load(std::memory_order_acquire)));
}

extern "C" int kudroid_looper_is_main(int64_t ptr) {
    return reinterpret_cast<LooperWaitSlot*>(ptr) ==
           g_mainLooperSlot.load(std::memory_order_acquire);
}
