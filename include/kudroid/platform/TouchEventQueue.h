#pragma once

#include <cstdint>
#include <deque>
#include <mutex>

namespace kudroid {

// Only consecutive MOVEs may merge; gesture boundaries stay ordered.
// The consumer is the Looper thread itself (MessageQueue.nativeDrainInput ->
// kuart_touch_drain_pending), which wakes through the looper wait slot, so the
// queue never needs its own condition variable.
class TouchEventQueue {
public:
    struct Event {
        int action = 0;
        float x = 0;
        float y = 0;
        // Fingers down including this event's own pointer. Native input code indexes
        // per-pointer arrays by the action's pointer index, so this must never be
        // smaller than index+1 — otherwise a second finger corrupts the heap.
        // Upper-bounded: nobody has that many fingers, and an absurd count would
        // size downstream arrays.
        int pointerCount = 1;
        static constexpr int kMaxPointerCount = 16;
    };

    static constexpr size_t kMaxQueueSize = 256;

    void reset(bool accepting) {
        std::lock_guard<std::mutex> lock(mutex_);
        accepting_ = accepting;
        events_.clear();
    }

    void push(int action, float x, float y, int pointerCount = 1) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!accepting_) return;
        if (pointerCount < 1) pointerCount = 1;
        if (pointerCount > Event::kMaxPointerCount) pointerCount = Event::kMaxPointerCount;
        // Coalesce the flood at ingress: a drag produces 60-120 MOVEs/s and each
        // one costs a full interpreted dispatch under the VM lock downstream.
        // Folding a MOVE into a queued trailing MOVE keeps the latest finger
        // position while bounding dispatches to gesture boundaries.
        if ((action & 0xff) == 2 && !events_.empty() &&
            (events_.back().action & 0xff) == 2) {
            events_.back().x = x;
            events_.back().y = y;
            if (pointerCount > events_.back().pointerCount) {
                events_.back().pointerCount = pointerCount;
            }
            return;
        }
        if (events_.size() >= kMaxQueueSize) {
            // Drop oldest MOVE if queue exceeds capacity to prevent unbounded growth.
            for (auto it = events_.begin(); it != events_.end(); ++it) {
                if ((it->action & 0xff) == 2) {
                    events_.erase(it);
                    break;
                }
            }
        }
        // Hard cap even with no MOVE to fold: a DOWN-spam flood (faulty or
        // hostile producer) must not grow the deque — and buy a VM-locked
        // dispatch per entry — without bound. Newest wins; a drop counter
        // would only add a log line to a flood.
        if (events_.size() >= kMaxQueueSize) {
            events_.pop_front();
        }
        events_.push_back(Event{action, x, y, pointerCount});
    }

    bool tryPop(Event& event) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (events_.empty()) return false;
        event = events_.front();
        events_.pop_front();
        return true;
    }

private:
    std::mutex mutex_;
    std::deque<Event> events_;
    bool accepting_ = false;
};

}  // namespace kudroid
