#pragma once

#include <cstdint>
#include <cstring>
#include <deque>
#include <mutex>
#include <vector>

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
        int pointerCount = 1;
        // One entry per index in the event's pointer array. Android delivers every live
        // finger in one event, each with its own id and position; a queue entry carrying
        // only the primary coordinate cannot represent a multi-finger drag at all.
        //
        // Sized by the event, not by a constant: a fixed table has to clamp, and a
        // clamped event is one whose tails the app cannot see (the finger is there on
        // the digitiser, missing from the event). Anything the device reports is kept.
        std::vector<int32_t> pointerIds;
        std::vector<float> pointerXs;
        std::vector<float> pointerYs;

        // Single-position form: pointer i is id i at (x, y).
        void setUniform(int a, float x_, float y_, int count) {
            action = a;
            x = x_;
            y = y_;
            if (count < 1) count = 1;
            pointerCount = count;
            pointerIds.assign(static_cast<size_t>(count), 0);
            pointerXs.assign(static_cast<size_t>(count), x_);
            pointerYs.assign(static_cast<size_t>(count), y_);
            for (int i = 0; i < count; ++i) pointerIds[i] = i;
        }
    };

    static constexpr size_t kMaxQueueSize = 256;

    void reset(bool accepting) {
        std::lock_guard<std::mutex> lock(mutex_);
        accepting_ = accepting;
        events_.clear();
    }

    void push(int action, float x, float y, int pointerCount = 1) {
        push(action, x, y, pointerCount, nullptr, nullptr, nullptr);
    }

    // Push with the producer's own pointer table (ids/xs/ys indexed by the action's
    // pointer index). A table shorter than pointerCount is ignored in favour of the
    // single-position form: a malformed producer must not invent pointers.
    void push(int action, float x, float y, int pointerCount, const int* ids, const float* xs,
              const float* ys) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!accepting_) return;
        if (pointerCount < 1) pointerCount = 1;
        const bool haveTable = ids != nullptr && xs != nullptr && ys != nullptr;
        // Coalesce the flood at ingress: a drag produces 60-120 MOVEs/s and each
        // one costs a full interpreted dispatch under the VM lock downstream.
        // Folding a MOVE into a queued trailing MOVE keeps the latest finger
        // positions while bounding dispatches to gesture boundaries. The fold needs
        // the same pointer set on both sides — a MOVE that gained or lost a finger is
        // a different gesture state and is enqueued instead.
        if ((action & 0xff) == 2 && !events_.empty() &&
            (events_.back().action & 0xff) == 2 && events_.back().pointerCount == pointerCount) {
            Event& back = events_.back();
            back.x = x;
            back.y = y;
            if (haveTable) {
                back.pointerIds.resize(static_cast<size_t>(pointerCount));
                back.pointerXs.resize(static_cast<size_t>(pointerCount));
                back.pointerYs.resize(static_cast<size_t>(pointerCount));
                for (int i = 0; i < pointerCount; ++i) {
                    back.pointerIds[i] = ids[i];
                    back.pointerXs[i] = xs[i];
                    back.pointerYs[i] = ys[i];
                }
                back.x = back.pointerXs[0];
                back.y = back.pointerYs[0];
            } else {
                back.setUniform(back.action, x, y, pointerCount);
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
        Event ev;
        ev.setUniform(action, x, y, pointerCount);
        if (haveTable) {
            for (int i = 0; i < pointerCount; ++i) {
                ev.pointerIds[i] = ids[i];
                ev.pointerXs[i] = xs[i];
                ev.pointerYs[i] = ys[i];
            }
            ev.x = ev.pointerXs[0];
            ev.y = ev.pointerYs[0];
        }
        events_.push_back(ev);
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
