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
        uint64_t generation = 0;
    };

    static constexpr size_t kMaxQueueSize = 256;

    void reset(bool accepting) {
        std::lock_guard<std::mutex> lock(mutex_);
        ++generation_;
        accepting_ = accepting;
        events_.clear();
    }

    void push(int action, float x, float y) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!accepting_) return;
            // Coalesce the flood at ingress: a drag produces 60-120 MOVEs/s and each
            // one costs a full interpreted dispatch under the VM lock downstream.
            // Folding a MOVE into a queued trailing MOVE keeps the latest finger
            // position while bounding worker wakeups to gesture boundaries.
            if ((action & 0xff) == 2 && !events_.empty() &&
                (events_.back().action & 0xff) == 2 &&
                events_.back().generation == generation_) {
                events_.back().x = x;
                events_.back().y = y;
                ready_.notify_one();
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
            events_.push_back(Event{action, x, y, generation_});
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
    bool accepting_ = false;
};

}  // namespace kudroid
