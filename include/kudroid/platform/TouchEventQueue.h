#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>

namespace kudroid {

// Only consecutive MOVEs may merge; gesture boundaries stay ordered.
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
        uint64_t generation = 0;
    };

    static constexpr size_t kMaxQueueSize = 256;

    void reset(bool accepting) {
        std::lock_guard<std::mutex> lock(mutex_);
        ++generation_;
        accepting_ = accepting;
        events_.clear();
        folded_moves_ = 0;
    }

    void push(int action, float x, float y, int pointerCount = 1) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!accepting_) return;
            if (pointerCount < 1) pointerCount = 1;
            // Coalesce the flood at ingress: a drag produces 60-120 MOVEs/s and each
            // one costs a full interpreted dispatch under the VM lock downstream.
            // Folding a MOVE into a queued trailing MOVE keeps the latest finger
            // position while bounding worker wakeups to gesture boundaries. No notify:
            // the queue was non-empty so no waiter can be parked on empty.
            if ((action & 0xff) == 2 && !events_.empty() &&
                (events_.back().action & 0xff) == 2 &&
                events_.back().generation == generation_) {
                events_.back().x = x;
                events_.back().y = y;
                if (pointerCount > events_.back().pointerCount) {
                    events_.back().pointerCount = pointerCount;
                }
                ++folded_moves_;
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
            events_.push_back(Event{action, x, y, pointerCount, generation_});
        }
        ready_.notify_one();
    }

    Event waitPop() {
        std::unique_lock<std::mutex> lock(mutex_);
        ready_.wait(lock, [this] { return !events_.empty(); });
        Event event = events_.front();
        events_.pop_front();
        return event;
    }

    // Pop, folding any run of consecutive MOVEs into the newest sample so the worker
    // pays one VM-locked dispatch per batch instead of one per raw event. Gesture
    // boundaries (DOWN/UP/CANCEL/POINTER_*) always stop the fold.
    Event popCoalesced() {
        std::unique_lock<std::mutex> lock(mutex_);
        ready_.wait(lock, [this] { return !events_.empty(); });
        Event event = events_.front();
        events_.pop_front();
        if ((event.action & 0xff) == 2) {
            while (!events_.empty() && (events_.front().action & 0xff) == 2 &&
                   events_.front().generation == event.generation) {
                event = events_.front();
                events_.pop_front();
                ++folded_moves_;
            }
        }
        return event;
    }

    // MOVEs folded since the last call; reported on the next DOWN/UP log line so a
    // drag session shows raw-vs-delivered rates with zero per-MOVE logging.
    uint64_t takeFoldedMoves() {
        std::lock_guard<std::mutex> lock(mutex_);
        uint64_t folded = folded_moves_;
        folded_moves_ = 0;
        return folded;
    }

    bool tryPop(Event& event) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (events_.empty()) return false;
        event = events_.front();
        events_.pop_front();
        return true;
    }

    bool isCurrent(const Event& event) {
        std::lock_guard<std::mutex> lock(mutex_);
        return accepting_ && event.generation == generation_;
    }

private:
    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<Event> events_;
    uint64_t generation_ = 0;
    uint64_t folded_moves_ = 0;
    bool accepting_ = false;
};

}  // namespace kudroid
