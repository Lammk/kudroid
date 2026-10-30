// test_touch_pointers.cpp — host-side regression tests for the touch pointer table in
// src/platform/InputShim.cpp, driven through the same AInputQueue API a guest uses.
//
// Covered regressions:
//   1. A wide batch keeps every finger. The table was a fixed 10-slot array and the
//      queue event a 16-slot one; a wider batch was clamped, and the indices past the
//      clamp read as another finger's position (or as the primary's, for a fill).
//   2. A finger lifting while others stay down is ACTION_POINTER_UP at that finger's own
//      index. Reporting ACTION_UP there ends the gesture for every finger in the app's
//      eyes, which is what left a second finger with no pointer to belong to.
//   3. The last finger lifting is ACTION_UP.
//   4. A MOVE batch moves every finger, not only the one the callback named.
//   5. Consecutive MOVEs coalesce: the guest drains one event, not a queue of samples.
#include "kudroid/platform/InputShim.h"
#include "kudroid/platform/GuestClock.h"

#include <cstdint>
#include <cstdio>
#include <unistd.h>
#include <initializer_list>

extern "C" {
// Injected from the iOS shell. Declared here as the guest's ABI sees them.
void kudroid_inject_touch_batch(int action, int primaryId, int count, const int* ids,
                                const float* xs, const float* ys);
int32_t bionic_AInputEvent_getType(const void*);
int32_t bionic_AInputEvent_getSource(const void*);
int32_t bionic_AKeyEvent_getAction(const void*);
int32_t bionic_AKeyEvent_getKeyCode(const void*);
int64_t bionic_AKeyEvent_getDownTime(const void*);
int64_t bionic_AKeyEvent_getEventTime(const void*);
int64_t bionic_AMotionEvent_getDownTime(const void*);
int64_t bionic_AMotionEvent_getEventTime(const void*);
void* bionic_ALooper_prepare(int);
int bionic_ALooper_pollOnce(int, int*, int*, void**);
int bionic_ALooper_addFd(void*, int, int, int, void*, void*);
int bionic_ALooper_removeFd(void*, int);
void bionic_AInputQueue_attachLooper(void*, void*, int, void*, void*);
void bionic_AInputQueue_detachLooper(void*);
int32_t bionic_AInputQueue_getEvent(void* queue, void** outEvent);
int32_t bionic_AInputQueue_finishEvent(void* queue, void* event, int handled);
int32_t bionic_AInputQueue_hasEvents(void* queue);
int32_t bionic_AMotionEvent_getAction(const void* event);
size_t bionic_AMotionEvent_getPointerCount(const void* event);
float bionic_AMotionEvent_getX(const void* event, size_t pointer_index);
float bionic_AMotionEvent_getY(const void* event, size_t pointer_index);
int32_t bionic_AMotionEvent_getPointerId(const void* event, size_t pointer_index);
}

namespace {

int failures = 0;

void Check(bool ok, const char* message) {
    std::printf("%s %s\n", ok ? "OK" : "FAIL", message);
    if (!ok) ++failures;
}

constexpr int kActionDown = 0;
constexpr int kActionUp = 1;
constexpr int kActionMove = 2;
constexpr int kActionCancel = 3;
constexpr int kActionPointerDown = 5;
constexpr int kActionPointerUp = 6;
constexpr int kPointerIndexShift = 8;

int MaskedAction(int action) { return action & 0xff; }
int ActionIndex(int action) { return action >> kPointerIndexShift; }

// Take the next queued event and hand it back finished, so the queue moves on.
const void* NextEvent(void* queue, bool* got) {
    void* event = nullptr;
    if (bionic_AInputQueue_getEvent(queue, &event) != 0) event = nullptr;
    *got = event != nullptr;
    return event;
}

void Finish(void* queue, const void* event) {
    bionic_AInputQueue_finishEvent(queue, const_cast<void*>(event), 1);
}

constexpr int kWide = 12;

}  // namespace

int main() {
    void* queue = kudroid_get_input_queue();
    if (queue == nullptr) {
        std::printf("FAIL no input queue\n");
        return 1;
    }

    int ids[kWide];
    float xs[kWide];
    float ys[kWide];
    for (int i = 0; i < kWide; ++i) {
        ids[i] = i;
        xs[i] = 10.0f * static_cast<float>(i);
        ys[i] = 20.0f * static_cast<float>(i);
    }

    // Twelve fingers land together; the last one is the finger this callback is about.
    kudroid_inject_touch_batch(kActionDown, kWide - 1, kWide, ids, xs, ys);
    bool got = false;
    const void* ev = NextEvent(queue, &got);
    Check(got, "a down batch reaches the queue");
    Check(got && bionic_AMotionEvent_getPointerCount(ev) == static_cast<size_t>(kWide),
          "every finger of a wide batch is in the event");
    Check(got && MaskedAction(bionic_AMotionEvent_getAction(ev)) == kActionPointerDown &&
              ActionIndex(bionic_AMotionEvent_getAction(ev)) == kWide - 1,
          "the new finger is the POINTER_DOWN index");
    bool intact = got;
    for (int i = 0; i < kWide && intact; ++i) {
        intact = bionic_AMotionEvent_getPointerId(ev, static_cast<size_t>(i)) == ids[i] &&
                 bionic_AMotionEvent_getX(ev, static_cast<size_t>(i)) == xs[i] &&
                 bionic_AMotionEvent_getY(ev, static_cast<size_t>(i)) == ys[i];
    }
    Check(intact, "each finger keeps its own id and position");
    if (got) Finish(queue, ev);

    // Finger 3 lifts while the rest stay down. The batch still carries it (that slot is
    // what POINTER_UP's index refers to) and the caller drops it afterwards.
    kudroid_inject_touch_batch(kActionUp, 3, kWide, ids, xs, ys);
    ev = NextEvent(queue, &got);
    Check(got && MaskedAction(bionic_AMotionEvent_getAction(ev)) == kActionPointerUp &&
              ActionIndex(bionic_AMotionEvent_getAction(ev)) == 3,
          "a lift among fingers still down is POINTER_UP at its own index");
    Check(got && bionic_AMotionEvent_getPointerCount(ev) == static_cast<size_t>(kWide),
          "the lifting finger is still in the event it is reported by");
    if (got) Finish(queue, ev);

    // The final finger lifts: that ends the gesture, so it is ACTION_UP.
    kudroid_inject_touch_batch(kActionUp, 0, 1, ids, xs, ys);
    ev = NextEvent(queue, &got);
    Check(got && bionic_AMotionEvent_getAction(ev) == kActionUp,
          "the last finger lifting is ACTION_UP");
    if (got) Finish(queue, ev);

    // A cancel ends every finger, whatever their number.
    kudroid_inject_touch_batch(kActionMove, 1, kWide, ids, xs, ys);
    ev = NextEvent(queue, &got);
    if (got) Finish(queue, ev);
    kudroid_inject_touch_batch(kActionCancel, 0, kWide, ids, xs, ys);
    ev = NextEvent(queue, &got);
    Check(got && bionic_AMotionEvent_getAction(ev) == kActionCancel,
          "cancel is ACTION_CANCEL");
    if (got) Finish(queue, ev);

    // A MOVE moves every finger, not only the one the callback named.
    float movedX[kWide];
    float movedY[kWide];
    for (int i = 0; i < kWide; ++i) {
        movedX[i] = xs[i] + 1.0f;
        movedY[i] = ys[i] + 2.0f;
    }
    kudroid_inject_touch_batch(kActionMove, 5, kWide, ids, movedX, movedY);
    ev = NextEvent(queue, &got);
    Check(got && MaskedAction(bionic_AMotionEvent_getAction(ev)) == kActionMove,
          "a move batch is ACTION_MOVE");
    bool allMoved = got &&
                    bionic_AMotionEvent_getPointerCount(ev) == static_cast<size_t>(kWide);
    for (int i = 0; i < kWide && allMoved; ++i) {
        allMoved = bionic_AMotionEvent_getX(ev, static_cast<size_t>(i)) == movedX[i] &&
                   bionic_AMotionEvent_getY(ev, static_cast<size_t>(i)) == movedY[i];
    }
    Check(allMoved, "a move batch moves every finger");
    if (got) Finish(queue, ev);

    // A drag arrives far faster than it is consumed; the guest must drain one event
    // holding the newest positions rather than a queue of samples to catch up on.
    float newestX[kWide];
    float newestY[kWide];
    for (int i = 0; i < kWide; ++i) {
        newestX[i] = movedX[i] + 100.0f;
        newestY[i] = movedY[i] + 100.0f;
    }
    kudroid_inject_touch_batch(kActionMove, 5, kWide, ids, movedX, movedY);
    kudroid_inject_touch_batch(kActionMove, 5, kWide, ids, newestX, newestY);
    ev = NextEvent(queue, &got);
    Check(got && bionic_AMotionEvent_getX(ev, 5) == newestX[5],
          "consecutive moves fold into the newest sample");
    // Finished events release their independent storage.
    if (got) Finish(queue, ev);
    Check(!bionic_AInputQueue_hasEvents(queue), "the folded move leaves no residue");

    kudroid_inject_touch_batch(kActionCancel, 0, kWide, ids, xs, ys);
    ev = NextEvent(queue, &got); if (got) Finish(queue, ev);
    const int64_t before = kudroid::guest_monotonic_ns();
    kudroid_inject_touch_batch(kActionDown, 0, 1, ids, xs, ys);
    kudroid_inject_touch_batch(kActionMove, 0, 1, ids, xs, ys);
    kudroid_inject_touch_batch(kActionMove, 0, 1, ids, xs, ys);
    kudroid_inject_touch_batch(kActionUp, 0, 1, ids, xs, ys);
    const int64_t after = kudroid::guest_monotonic_ns();
    int64_t downTime = 0;
    for (int action : {kActionDown, kActionMove, kActionUp}) {
        ev = NextEvent(queue, &got);
        if (got && action == kActionDown) downTime = bionic_AMotionEvent_getEventTime(ev);
        Check(got && bionic_AMotionEvent_getAction(ev) == action
                  && bionic_AMotionEvent_getEventTime(ev) >= before
                  && bionic_AMotionEvent_getEventTime(ev) <= after
                  && bionic_AMotionEvent_getDownTime(ev) == downTime,
              "ordinary motion keeps gesture order, host arrival and down time");
        if (got) Finish(queue, ev);
    }
    kudroid_enqueue_key_event(0, 29, 100000000ll);
    kudroid_enqueue_key_event(1, 29, 100000000ll);
    for (int action : {0, 1}) {
        ev = NextEvent(queue, &got);
        Check(got && bionic_AInputEvent_getType(ev) == 1 && bionic_AInputEvent_getSource(ev) == 0x101
                  && bionic_AKeyEvent_getAction(ev) == action && bionic_AKeyEvent_getKeyCode(ev) == 29
                  && bionic_AKeyEvent_getDownTime(ev) == 100000000ll
                  && bionic_AKeyEvent_getEventTime(ev) >= 100000000ll,
              "native key carries type, source, action, code and timestamps");
        if (got) Finish(queue, ev);
    }

    void* empty = reinterpret_cast<void*>(1);
    Check(bionic_AInputQueue_getEvent(queue, &empty) < 0 && empty == nullptr,
          "empty input queue reports no event instead of successful null input");
    void* looper = bionic_ALooper_prepare(1);
    kudroid_inject_touch_batch(kActionDown, 0, 1, ids, xs, ys);
    kudroid_inject_touch_batch(kActionUp, 0, 1, ids, xs, ys);
    bionic_AInputQueue_attachLooper(queue, looper, 0, nullptr, nullptr);
    Check(kudroid_native_input_attached() != 0, "Looper identifier zero is a valid native input attachment");
    int ident = bionic_ALooper_pollOnce(0, nullptr, nullptr, nullptr);
    Check(ident == 0, "input queued before attach wakes the native Looper");
    ev = NextEvent(queue, &got); if (got) Finish(queue, ev);
    ident = bionic_ALooper_pollOnce(0, nullptr, nullptr, nullptr);
    Check(ident == 0, "remaining input stays ready after finishing one event");
    ident = bionic_ALooper_pollOnce(0, nullptr, nullptr, nullptr);
    Check(ident == 0, "polling preserves readiness until the input is consumed");
    ev = NextEvent(queue, &got); if (got) Finish(queue, ev);

    bionic_AInputQueue_detachLooper(queue);
    int otherPipe[2];
    if (::pipe(otherPipe) != 0) return 1;
    bionic_ALooper_addFd(looper, otherPipe[0], 9, 1, nullptr, nullptr);
    bionic_AInputQueue_attachLooper(queue, looper, 0, nullptr, nullptr);
    const char byte = 1;
    if (::write(otherPipe[1], &byte, 1) != 1) return 1;
    kudroid_inject_touch_batch(kActionDown, 0, 1, ids, xs, ys);
    ident = bionic_ALooper_pollOnce(0, nullptr, nullptr, nullptr);
    Check(ident == 9, "an earlier ready descriptor can be returned before input");
    char consumed;
    if (::read(otherPipe[0], &consumed, 1) != 1) return 1;
    ident = bionic_ALooper_pollOnce(0, nullptr, nullptr, nullptr);
    Check(ident == 0, "another ready descriptor cannot discard the input wakeup");
    ev = NextEvent(queue, &got); if (got) Finish(queue, ev);
    ident = bionic_ALooper_pollOnce(0, nullptr, nullptr, nullptr);
    Check(ident == -3, "an empty consumed input queue no longer wakes the Looper");
    bionic_ALooper_removeFd(looper, otherPipe[0]);
    ::close(otherPipe[0]);
    ::close(otherPipe[1]);
    kudroid_inject_touch_batch(kActionUp, 0, 1, ids, xs, ys);
    ev = NextEvent(queue, &got); if (got) Finish(queue, ev);
    bionic_AInputQueue_attachLooper(queue, looper, 7, nullptr, nullptr);
    kudroid_inject_touch_batch(kActionDown, 0, 1, ids, xs, ys);
    bionic_AInputQueue_detachLooper(queue);
    Check(kudroid_native_input_attached() == 0, "detach restores Java input routing");
    ident = bionic_ALooper_pollOnce(0, nullptr, nullptr, nullptr);
    Check(ident < 0, "detached input cannot wake the old Looper");
    ev = NextEvent(queue, &got); if (got) Finish(queue, ev);
    kudroid_inject_touch_batch(kActionUp, 0, 1, ids, xs, ys);
    ev = NextEvent(queue, &got); if (got) Finish(queue, ev);

    kudroid_inject_touch_batch(kActionDown, 0, 1, ids, xs, ys);
    kudroid_inject_touch_batch(kActionUp, 0, 1, ids, xs, ys);
    const void* first = NextEvent(queue, &got);
    const void* second = NextEvent(queue, &got);
    Check(first && second && first != second,
          "multiple fetched events keep distinct lifetimes until finished");
    Check(second && bionic_AMotionEvent_getAction(second) == kActionUp,
          "fetching the next event advances without waiting for the previous finish");
    if (first && second && first != second) {
        Check(bionic_AMotionEvent_getAction(first) == kActionDown,
              "fetching a later event preserves the earlier sample");
        Finish(queue, second);
        Check(bionic_AMotionEvent_getAction(first) == kActionDown,
              "events can be finished out of fetch order");
        Finish(queue, first);
        Check(bionic_AInputQueue_finishEvent(queue, const_cast<void*>(first), 1) < 0,
              "a repeated finish cannot delete another event");
    } else if (second) {
        Finish(queue, second);
    }
    while (bionic_AInputQueue_hasEvents(queue)) {
        ev = NextEvent(queue, &got); if (!got) break;
        Finish(queue, ev);
    }

    return failures ? 1 : 0;
}
