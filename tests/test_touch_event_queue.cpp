#include "kudroid/platform/TouchEventQueue.h"

#include <cstdio>

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
    int moveCount = 0;
    float lastMoveX = 0;
    while (queue.tryPop(event)) {
        if (event.action == 1) break;
        if (event.action == 2) {
            ++moveCount;
            lastMoveX = event.x;
        }
    }
    Check(lastMoveX == 10000.0f, "MOVE stream retains the newest point");
    Check(event.action == 1, "UP follows the final MOVE");
    Check(moveCount <= static_cast<int>(kudroid::TouchEventQueue::kMaxQueueSize),
          "MOVE flood stays bounded by max queue size");

    queue.reset(true);
    queue.push(0, 0, 0);
    for (int i = 1; i <= 500; ++i) queue.push(2, float(i), float(i));
    Check(queue.tryPop(event) && event.action == 0, "coalesce keeps DOWN first");
    Check(queue.tryPop(event) && event.action == 2 && event.x == 500.0f && event.y == 500.0f,
          "consecutive MOVEs fold into one with newest coords");
    Check(!queue.tryPop(event), "MOVE flood leaves no residue");

    queue.reset(true);
    queue.push(0, 0, 0, 2);
    Check(queue.tryPop(event) && event.pointerCount == 2, "pointerCount survives the queue");
    Check(event.pointerIds.size() == 2 && event.pointerXs.size() == 2 &&
              event.pointerYs.size() == 2,
          "the single-position form sizes the table to the event");

    // More fingers than a fixed table would hold must survive intact, each with its own
    // id and position: a clamped event reads as another finger's position in the app.
    queue.reset(true);
    const int manyIds[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    float manyXs[12];
    float manyYs[12];
    for (int i = 0; i < 12; ++i) {
        manyXs[i] = 100.0f + static_cast<float>(i);
        manyYs[i] = 200.0f + static_cast<float>(i);
    }
    queue.push(0, 100.0f, 200.0f, 12, manyIds, manyXs, manyYs);
    Check(queue.tryPop(event) && event.pointerCount == 12,
          "a twelve-finger event keeps every pointer");
    bool intact = event.pointerIds.size() == 12 && event.pointerXs.size() == 12 &&
                  event.pointerYs.size() == 12;
    for (int i = 0; i < 12 && intact; ++i) {
        intact = event.pointerIds[i] == i && event.pointerXs[i] == manyXs[i] &&
                 event.pointerYs[i] == manyYs[i];
    }
    Check(intact, "every pointer keeps its own id and position");

    const int actions[] = {0, 2, 1, 0, 2, 3};
    for (int action : actions) queue.push(action, 0, 0);
    for (int action : actions) {
        Check(queue.tryPop(event) && event.action == action,
              "coalescing preserves gesture boundaries and CANCEL");
    }
    queue.push(2, 5, 5);
    queue.reset(false);
    Check(!queue.tryPop(event), "reset removes pending events");
    queue.reset(true);
    Check(!queue.tryPop(event), "a new session starts empty");

    // drainCoalesced batches and collapses consecutive MOVEs into the latest point.
    queue.reset(true);
    queue.push(0, 0, 0);  // DOWN
    for (int i = 1; i <= 100; ++i) queue.push(2, float(i), float(i));  // 100 MOVEs
    queue.push(1, 100.0f, 100.0f);  // UP
    std::vector<kudroid::TouchEventQueue::Event> drainedEvents;
    size_t count = queue.drainCoalesced(drainedEvents, 16);
    Check(count == 3 && drainedEvents.size() == 3, "drainCoalesced collapses MOVE stream to 3 events");
    Check(drainedEvents[0].action == 0, "first event is DOWN");
    Check(drainedEvents[1].action == 2 && drainedEvents[1].x == 100.0f && drainedEvents[1].y == 100.0f,
          "coalesced MOVE carries latest coordinates");
    Check(drainedEvents[2].action == 1, "third event is UP");

    return failures ? 1 : 0;
}
