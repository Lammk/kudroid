// Frame pacing: AChoreographer and ANativeWindow_setFrameRate.
//
// Why this exists. ULTRAKILL launched, brought up Vulkan on the A13, created its
// swapchain — and then stopped, permanently, with no crash and nothing in the log
// after the last symbol lookup. The lookups were these:
//
//     missing symbol bound to dummy: ANativeWindow_setFrameRate
//     missing symbol bound to dummy: AChoreographer_getInstance
//     missing symbol bound to dummy: AChoreographer_postFrameCallback
//     ...
//     thread-create                       tid=UnityMain
//     cond-wait-enter cond=0x300f15e10    tid=UnityMain      <-- forever
//     thread-entry tid=3711786
//     thread-exit  tid=3711786            <-- started and left immediately
//
// All five bound to kudroid_universal_dummy, which returns 0. So getInstance()
// returned NULL, Unity's frame-pacer thread had nothing to post a callback to and
// exited without ever signalling the condition variable UnityMain was waiting on.
// UnityMain sat in __psynch_cvwait for the rest of the session: five thread samples
// forty seconds apart, identical pc, identical sp, cpu_ms unchanged at 383.
// UnityGfxDeviceWorker never accumulated a millisecond, so not one frame was ever
// submitted.
//
// A dummy that returns 0 is the worst possible answer for a factory function. It is
// indistinguishable from "no Choreographer on this thread", which on Android is a
// real and rare condition that callers handle by giving up — so the guest did
// exactly what it was written to do, and the failure surfaced as a hang in a
// condition variable several layers away from the cause.
//
// What this provides. A real Choreographer: per-thread instances, one-shot frame
// callbacks with monotonic timestamps on the same clock the guest reads through
// clock_gettime(CLOCK_MONOTONIC), refresh-rate callbacks carrying the host display's
// actual vsync period, and a pacer thread that only runs while something is waiting
// on a frame.
#ifndef KUDROID_PLATFORM_FRAMEPACER_H
#define KUDROID_PLATFORM_FRAMEPACER_H

#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

// ── AChoreographer (NDK) ────────────────────────────────────────────────────
//
// getInstance never returns NULL here. On Android it does when the calling thread
// has no Looper; KuDroid's ALooper_forThread creates one on demand, so the
// precondition always holds — and returning NULL is precisely the failure that
// wedged ULTRAKILL.
void* bionic_AChoreographer_getInstance(void);

// One-shot, as on Android: the callback must re-post itself to receive another
// frame. `callback` is void(long frameTimeNanos, void* data); on arm64 `long` is
// 64-bit, so this and the ...64 form below share one ABI.
void bionic_AChoreographer_postFrameCallback(void* choreographer, void* callback,
                                             void* data);
void bionic_AChoreographer_postFrameCallbackDelayed(void* choreographer, void* callback,
                                                    void* data, long delayMillis);
void bionic_AChoreographer_postFrameCallback64(void* choreographer, void* callback,
                                               void* data);
void bionic_AChoreographer_postFrameCallbackDelayed64(void* choreographer, void* callback,
                                                      void* data, uint32_t delayMillis);

// Refresh-rate callbacks: void(int64_t vsyncPeriodNanos, void* data). Android
// invokes one immediately on registration with the current period, then again on
// every change. Registering is what a frame pacer uses to size its budget, so the
// immediate call matters: without it the pacer has no period until the display
// changes, which on a fixed-rate display is never.
void bionic_AChoreographer_registerRefreshRateCallback(void* choreographer, void* callback,
                                                       void* data);
void bionic_AChoreographer_unregisterRefreshRateCallback(void* choreographer, void* callback,
                                                         void* data);

// ── ANativeWindow frame rate (NDK, API 30/31) ───────────────────────────────
//
// A hint, and treated as one: the requested rate caps how often the pacer delivers
// frame callbacks, so a guest that asks for 30 fps is not woken 60 times a second.
// 0.0f means "no preference" and restores the display's own rate. Returns 0 on
// success, as Android does.
int32_t bionic_ANativeWindow_setFrameRate(void* window, float frameRate, int8_t compatibility);
int32_t bionic_ANativeWindow_setFrameRateWithChangeStrategy(void* window, float frameRate,
                                                            int8_t compatibility,
                                                            int8_t changeFrameRateStrategy);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

namespace kudroid {

// ── bridge, for the extern "C" entry points above ───────────────────────────
//
// The state lives in an anonymous namespace inside FramePacer.cpp so nothing else
// can reach into it. These are the only doors.
void* frame_pacer_instance_for_this_thread();
void frame_pacer_post(void* instance, void* callback, void* data, uint64_t delay_ns);
void frame_pacer_add_refresh_callback(void* instance, void* callback, void* data);
void frame_pacer_remove_refresh_callback(void* instance, void* callback, void* data);
void frame_pacer_request_rate(float frame_rate);

// The host display's vsync period in nanoseconds. Queried from the platform once
// and cached; 60 Hz until the answer arrives, and if it never does.
//
// Reported to the guest verbatim through the refresh-rate callback, so a 120 Hz
// device is described as one. Inventing 60 there would make a frame pacer aim at a
// deadline twice as far away as the real one on every frame.
int64_t display_vsync_period_ns();

// The interval the pacer is currently delivering frames at: the display period,
// or the longer period a setFrameRate hint asked for.
int64_t frame_pacer_interval_ns();

// How many frame callbacks are queued across every thread. For tests, and for a
// summary line.
int frame_pacer_pending_count();

// Test seam: drop every instance and queued callback, stop the pacer thread, and
// forget any setFrameRate hint. Not for use on a live guest.
void frame_pacer_reset_for_test();

// Deliver every frame callback that is due right now, on the calling thread,
// without waiting for the pacer's next tick. Returns how many ran.
//
// The pacer exists as a fallback; this is the primary path. Android dispatches
// Choreographer callbacks through the Looper of the thread that registered them,
// and a guest callback that touches GL state or thread-locals has to run there. So
// the pipe registered with the looper wakes the guest's own thread, the looper
// invokes this, and the pacer only steps in for a callback nobody came to collect.
int frame_pacer_dispatch_due();

}  // namespace kudroid

#endif  // __cplusplus

#endif  // KUDROID_PLATFORM_FRAMEPACER_H
