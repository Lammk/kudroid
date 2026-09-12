#include "kudroid/platform/TouchEventQueue.h"

#include <cstdio>
#include <future>

namespace {
int failures = 0;
void Check(bool ok, const char* message) {
    std::printf("%s %s\n", ok ? "OK" : "FAIL", message);
    if (!ok) ++failures;
}
}

int main() {
    kudroid::TouchEventQueue queue;
    kudroid::TouchEventQueue::Event event;
    queue.push(0, 1, 1);
    Check(!queue.tryPop(event), "inactive sessions reject input");
    queue.reset(true);
    queue.push(0, 0, 0);
    for (int i = 1; i <= 10000; ++i) queue.push(2, float(i), float(i));
    queue.push(1, 10000, 10000);
    Check(queue.tryPop(event) && event.action == 0, "DOWN stays first");
    Check(queue.tryPop(event) && event.action == 2 && event.x == 10000,
          "MOVE flood retains the newest point");
    Check(queue.tryPop(event) && event.action == 1, "UP follows the final MOVE");
    Check(!queue.tryPop(event), "MOVE flood does not grow the queue");

    const int actions[] = {0, 2, 1, 0, 2, 3};
    for (int action : actions) queue.push(action, 0, 0);
    for (int action : actions) {
        Check(queue.tryPop(event) && event.action == action,
              "coalescing preserves gesture boundaries and CANCEL");
    }
    const auto old = event;
    queue.push(2, 5, 5);
    queue.reset(false);
    Check(!queue.isCurrent(old), "reset invalidates an already popped event");
    Check(!queue.tryPop(event), "reset removes pending events");
    queue.reset(true);
    Check(!queue.isCurrent(old), "a new session rejects old events");

    auto waiting = std::async(std::launch::async, [&queue] { return queue.waitPop(); });
    queue.push(0, 7, 8);
    event = waiting.get();
    Check(event.action == 0 && event.x == 7 && event.y == 8,
          "enqueue wakes the consumer");
    Check(queue.isCurrent(event), "new session events are valid");
    return failures ? 1 : 0;
}
