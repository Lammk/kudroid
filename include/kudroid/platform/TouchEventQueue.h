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
            Event event{action, x, y, generation_};
            if ((action & 0xff) == 2 && !events_.empty() &&
                (events_.back().action & 0xff) == 2) {
                events_.back() = event;
            } else {
                events_.push_back(event);
            }
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
