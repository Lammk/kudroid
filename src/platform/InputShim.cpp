#include "kudroid/platform/InputShim.h"
#include <cstdint>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <mutex>
#include <deque>
#include <chrono>

// Declared in SyscallShim.cpp.
extern "C" int bionic_ALooper_addFd(void* looper, int fd, int ident, int events,
                                    void* callback, void* data);
extern "C" void bionic_ALooper_markInputPipe(void* looper, int fd);

namespace kudroid {
namespace {

// ─────────────────────────────────────────────────────────────────────────────
// AInputEvent / AMotionEvent simulated structure
// ─────────────────────────────────────────────────────────────────────────────
struct BionicInputEvent {
    int32_t type;   // 2 = AINPUT_EVENT_TYPE_MOTION
    int32_t action;
    float x;
    float y;
    int64_t eventTime;
    int32_t pointerCount;
    int32_t source;
    int32_t flags;
};

// ─────────────────────────────────────────────────────────────────────────────
// AInputQueue — a mutex-protected FIFO of input events.
//
// Real Android apps call AInputQueue_getEvent() in a loop (usually from a
// thread attached to a Looper). We provide a proper FIFO so events are not
// lost, plus the standard AInputQueue_* API surface.
// ─────────────────────────────────────────────────────────────────────────────
struct BionicInputQueue {
    std::mutex mtx;
    std::deque<BionicInputEvent> events;
    int32_t id; // looper ident
    int wakePipe[2]; // pipe to wake the looper when events arrive
    bool pipeReady;

    BionicInputQueue() : id(0), pipeReady(false) {
        wakePipe[0] = -1;
        wakePipe[1] = -1;
    }
};

static BionicInputQueue g_inputQueue;

// Ensure the wake pipe exists (lazily created on first use).
// Lock nội bộ: kudroid_inject_touch_event (thread Swift) và attachLooper
// (thread game) có thể chạy đồng thời — không lock thì pipe() chạy 2 lần,
// leak một cặp fd.
static void ensure_wake_pipe(BionicInputQueue* q) {
    std::lock_guard<std::mutex> lock(q->mtx);
    if (q->pipeReady) return;
    if (::pipe(q->wakePipe) == 0) {
        // Set read end non-blocking.
        int flags = ::fcntl(q->wakePipe[0], F_GETFL, 0);
        ::fcntl(q->wakePipe[0], F_SETFL, flags | O_NONBLOCK);
        q->pipeReady = true;
    }
}

// Exported cho kudroid_bridge: con trỏ AInputQueue dùng để truyền vào
// callback onInputQueueCreated của ANativeActivity.
extern "C" void* kudroid_get_input_queue(void) {
    return &g_inputQueue;
}

// Exported for Swift to inject touch events
extern "C" void kudroid_inject_touch_event_multi(float x, float y, int32_t action, int32_t pointerId, int32_t pointerCount) {
    (void)pointerId;
    BionicInputEvent ev;
    ev.type = 2; // AINPUT_EVENT_TYPE_MOTION
    ev.action = action;
    ev.x = x;
    ev.y = y;
    ev.eventTime = static_cast<int64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    ev.pointerCount = pointerCount > 0 ? pointerCount : 1;
    ev.source = 0x0002; // AINPUT_SOURCE_TOUCHSCREEN
    ev.flags = 0;

    {
        std::lock_guard<std::mutex> lock(g_inputQueue.mtx);
        g_inputQueue.events.push_back(ev);
    }

    // Wake the looper so it processes the new event.
    ensure_wake_pipe(&g_inputQueue); // tự khóa nội bộ — an toàn thread
    if (g_inputQueue.pipeReady) {
        uint8_t byte = 1;
        ssize_t unused = ::write(g_inputQueue.wakePipe[1], &byte, 1);
        (void)unused;
    }
}

extern "C" void kudroid_inject_touch_event(float x, float y, int32_t action) {
    kudroid_inject_touch_event_multi(x, y, action, 0, 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// AInputQueue API (Bionic)
// ─────────────────────────────────────────────────────────────────────────────

extern "C" void* bionic_AInputQueue_create() {
    return &g_inputQueue;
}

extern "C" int32_t bionic_AInputQueue_getEvent(void* queue, void** outEvent) {
    if (!queue || !outEvent) return -1;
    BionicInputQueue* q = static_cast<BionicInputQueue*>(queue);
    std::lock_guard<std::mutex> lock(q->mtx);
    if (q->events.empty()) {
        *outEvent = nullptr;
        return 0; // No event available (WOULD_BLOCK semantics)
    }
    // Return a pointer to the front event. The caller must call
    // AInputQueue_finishEvent() to pop it.
    *outEvent = &q->events.front();
    return 0;
}

extern "C" int32_t bionic_AInputQueue_preDispatchEvent(void* queue, void* event) {
    (void)queue;
    (void)event;
    return 0; // Not predispatching
}

extern "C" int32_t bionic_AInputQueue_finishEvent(void* queue, void* event, int handled) {
    (void)handled;
    if (!queue || !event) return -1;
    BionicInputQueue* q = static_cast<BionicInputQueue*>(queue);
    std::lock_guard<std::mutex> lock(q->mtx);
    if (!q->events.empty() && &q->events.front() == event) {
        q->events.pop_front();
    }
    return 0;
}

extern "C" void bionic_AInputQueue_attachLooper(void* queue, void* looper, int ident,
                                                void* callback, void* data) {
    if (!queue) return;
    BionicInputQueue* q = static_cast<BionicInputQueue*>(queue);
    q->id = ident;

    // Register the wake pipe with the looper so poll() wakes on input.
    ensure_wake_pipe(q);
    if (q->pipeReady && looper) {
        bionic_ALooper_addFd(looper, q->wakePipe[0], ident, 0x0001 /* ALOOPER_EVENT_INPUT */,
                             callback, data);
        // Đánh dấu fd này là wake pipe của AInputQueue để ALooper_pollAll drain
        // nước mỗi khi nó readable — nếu không pipe sẽ luôn ready -> busy loop.
        bionic_ALooper_markInputPipe(looper, q->wakePipe[0]);
    }
}

extern "C" void bionic_AInputQueue_detachLooper(void* queue) {
    (void)queue;
}

extern "C" int32_t bionic_AInputQueue_hasEvents(void* queue) {
    if (!queue) return 0;
    BionicInputQueue* q = static_cast<BionicInputQueue*>(queue);
    std::lock_guard<std::mutex> lock(q->mtx);
    return q->events.empty() ? 0 : 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// AInputEvent / AMotionEvent getters
// ─────────────────────────────────────────────────────────────────────────────

extern "C" int32_t bionic_AInputEvent_getType(const void* event) {
    if (!event) return 0;
    return static_cast<const BionicInputEvent*>(event)->type;
}

extern "C" int32_t bionic_AInputEvent_getSource(const void* event) {
    if (!event) return 0;
    return static_cast<const BionicInputEvent*>(event)->source;
}

extern "C" int32_t bionic_AInputEvent_getFlags(const void* event) {
    if (!event) return 0;
    return static_cast<const BionicInputEvent*>(event)->flags;
}

extern "C" int64_t bionic_AInputEvent_getEventTime(const void* event) {
    if (!event) return 0;
    return static_cast<const BionicInputEvent*>(event)->eventTime;
}

extern "C" int32_t bionic_AMotionEvent_getAction(const void* event) {
    if (!event) return 0;
    return static_cast<const BionicInputEvent*>(event)->action;
}

extern "C" float bionic_AMotionEvent_getX(const void* event, size_t pointer_index) {
    (void)pointer_index; // Single touch for now
    if (!event) return 0.0f;
    return static_cast<const BionicInputEvent*>(event)->x;
}

extern "C" float bionic_AMotionEvent_getY(const void* event, size_t pointer_index) {
    (void)pointer_index;
    if (!event) return 0.0f;
    return static_cast<const BionicInputEvent*>(event)->y;
}

extern "C" float bionic_AMotionEvent_getXByIndex(const void* event, size_t pointer_index) {
    return bionic_AMotionEvent_getX(event, pointer_index);
}

extern "C" float bionic_AMotionEvent_getYByIndex(const void* event, size_t pointer_index) {
    return bionic_AMotionEvent_getY(event, pointer_index);
}

extern "C" float bionic_AMotionEvent_getPressure(const void* event, size_t pointer_index) {
    (void)event; (void)pointer_index;
    return 1.0f;
}

extern "C" float bionic_AMotionEvent_getPressureByIndex(const void* event, size_t pointer_index) {
    (void)event; (void)pointer_index;
    return 1.0f;
}

extern "C" size_t bionic_AMotionEvent_getPointerCount(const void* event) {
    if (!event) return 0;
    return static_cast<const BionicInputEvent*>(event)->pointerCount;
}

extern "C" int32_t bionic_AMotionEvent_getPointerId(const void* event, size_t pointer_index) {
    (void)event;
    return static_cast<int32_t>(pointer_index);
}

extern "C" float bionic_AMotionEvent_getRawX(const void* event, size_t pointer_index) {
    return bionic_AMotionEvent_getX(event, pointer_index);
}

extern "C" float bionic_AMotionEvent_getRawY(const void* event, size_t pointer_index) {
    return bionic_AMotionEvent_getY(event, pointer_index);
}

extern "C" int64_t bionic_AMotionEvent_getDownTime(const void* event) {
    if (!event) return 0;
    return static_cast<const BionicInputEvent*>(event)->eventTime;
}

extern "C" int64_t bionic_AMotionEvent_getEventTime(const void* event) {
    if (!event) return 0;
    return static_cast<const BionicInputEvent*>(event)->eventTime;
}

extern "C" size_t bionic_AMotionEvent_getHistorySize(const void* event) {
    (void)event; return 0;
}

extern "C" int64_t bionic_AMotionEvent_getHistoricalEventTime(const void* event, size_t history_index) {
    (void)history_index;
    return bionic_AMotionEvent_getEventTime(event);
}

extern "C" float bionic_AMotionEvent_getHistoricalX(const void* event, size_t pointer_index, size_t history_index) {
    (void)history_index;
    return bionic_AMotionEvent_getX(event, pointer_index);
}

extern "C" float bionic_AMotionEvent_getHistoricalY(const void* event, size_t pointer_index, size_t history_index) {
    (void)history_index;
    return bionic_AMotionEvent_getY(event, pointer_index);
}

extern "C" float bionic_AMotionEvent_getHistoricalPressure(const void* event, size_t pointer_index, size_t history_index) {
    (void)event; (void)pointer_index; (void)history_index;
    return 1.0f;
}

// ─────────────────────────────────────────────────────────────────────────────
// AKeyEvent shims
// ─────────────────────────────────────────────────────────────────────────────
extern "C" int32_t bionic_AKeyEvent_getAction(const void* event) {
    if (!event) return 0;
    return static_cast<const BionicInputEvent*>(event)->action;
}

extern "C" int32_t bionic_AKeyEvent_getKeyCode(const void* event) {
    (void)event; return 0;
}

extern "C" int32_t bionic_AKeyEvent_getScanCode(const void* event) {
    (void)event; return 0;
}

extern "C" int32_t bionic_AKeyEvent_getMetaState(const void* event) {
    (void)event; return 0;
}

extern "C" int32_t bionic_AKeyEvent_getRepeatCount(const void* event) {
    (void)event; return 0;
}

extern "C" int64_t bionic_AKeyEvent_getDownTime(const void* event) {
    if (!event) return 0;
    return static_cast<const BionicInputEvent*>(event)->eventTime;
}

extern "C" int64_t bionic_AKeyEvent_getEventTime(const void* event) {
    if (!event) return 0;
    return static_cast<const BionicInputEvent*>(event)->eventTime;
}

// ─────────────────────────────────────────────────────────────────────────────
// Bionic ASensorManager shims (unchanged)
// ─────────────────────────────────────────────────────────────────────────────
extern "C" void* bionic_ASensorManager_getInstance() {
    static int dummyManager = 1;
    return &dummyManager;
}

extern "C" void* bionic_ASensorManager_createEventQueue(void* manager, void* looper, int ident, void* callback, void* data) {
    (void)manager; (void)looper; (void)ident; (void)callback; (void)data;
    static int dummyQueue = 1;
    return &dummyQueue;
}

extern "C" int bionic_ASensorManager_destroyEventQueue(void* manager, void* queue) {
    (void)manager; (void)queue; return 0;
}

extern "C" int bionic_ASensorManager_getSensorList(void* manager, void** list) {
    (void)manager; (void)list; return 0; // 0 sensors
}

extern "C" void* bionic_ASensorManager_getDefaultSensor(void* manager, int type) {
    (void)manager; (void)type; return nullptr;
}

extern "C" int bionic_ASensorEventQueue_enableSensor(void* queue, void* sensor) { (void)queue; (void)sensor; return 0; }
extern "C" int bionic_ASensorEventQueue_disableSensor(void* queue, void* sensor) { (void)queue; (void)sensor; return 0; }
extern "C" int bionic_ASensorEventQueue_setEventRate(void* queue, void* sensor, int32_t usec) { (void)queue; (void)sensor; (void)usec; return 0; }
extern "C" int bionic_ASensorEventQueue_hasEvents(void* queue) { (void)queue; return 0; }
extern "C" ssize_t bionic_ASensorEventQueue_getEvents(void* queue, void* events, size_t count) {
    (void)queue; (void)events; (void)count;
    return 0; // Sensors shouldn't return touch events
}
extern "C" const char* bionic_ASensor_getName(void* sensor) { (void)sensor; return "DummySensor"; }
extern "C" const char* bionic_ASensor_getVendor(void* sensor) { (void)sensor; return "Kudroid"; }
extern "C" int bionic_ASensor_getType(void* sensor) { (void)sensor; return 1; }
extern "C" float bionic_ASensor_getResolution(void* sensor) { (void)sensor; return 1.0f; }
extern "C" int bionic_ASensor_getMinDelay(void* sensor) { (void)sensor; return 10000; }

const SymbolEntry kInputSymbols[] = {
    // Sensor Symbols
    {"ASensorManager_getInstance", reinterpret_cast<void*>(&bionic_ASensorManager_getInstance)},
    {"ASensorManager_createEventQueue", reinterpret_cast<void*>(&bionic_ASensorManager_createEventQueue)},
    {"ASensorManager_destroyEventQueue", reinterpret_cast<void*>(&bionic_ASensorManager_destroyEventQueue)},
    {"ASensorManager_getSensorList", reinterpret_cast<void*>(&bionic_ASensorManager_getSensorList)},
    {"ASensorManager_getDefaultSensor", reinterpret_cast<void*>(&bionic_ASensorManager_getDefaultSensor)},
    {"ASensorEventQueue_enableSensor", reinterpret_cast<void*>(&bionic_ASensorEventQueue_enableSensor)},
    {"ASensorEventQueue_disableSensor", reinterpret_cast<void*>(&bionic_ASensorEventQueue_disableSensor)},
    {"ASensorEventQueue_setEventRate", reinterpret_cast<void*>(&bionic_ASensorEventQueue_setEventRate)},
    {"ASensorEventQueue_hasEvents", reinterpret_cast<void*>(&bionic_ASensorEventQueue_hasEvents)},
    {"ASensorEventQueue_getEvents", reinterpret_cast<void*>(&bionic_ASensorEventQueue_getEvents)},
    {"ASensor_getName", reinterpret_cast<void*>(&bionic_ASensor_getName)},
    {"ASensor_getVendor", reinterpret_cast<void*>(&bionic_ASensor_getVendor)},
    {"ASensor_getType", reinterpret_cast<void*>(&bionic_ASensor_getType)},
    {"ASensor_getResolution", reinterpret_cast<void*>(&bionic_ASensor_getResolution)},
    {"ASensor_getMinDelay", reinterpret_cast<void*>(&bionic_ASensor_getMinDelay)},

    // InputQueue Symbols
    {"AInputQueue_create", reinterpret_cast<void*>(&bionic_AInputQueue_create)},
    {"AInputQueue_getEvent", reinterpret_cast<void*>(&bionic_AInputQueue_getEvent)},
    {"AInputQueue_preDispatchEvent", reinterpret_cast<void*>(&bionic_AInputQueue_preDispatchEvent)},
    {"AInputQueue_finishEvent", reinterpret_cast<void*>(&bionic_AInputQueue_finishEvent)},
    {"AInputQueue_attachLooper", reinterpret_cast<void*>(&bionic_AInputQueue_attachLooper)},
    {"AInputQueue_detachLooper", reinterpret_cast<void*>(&bionic_AInputQueue_detachLooper)},
    {"AInputQueue_hasEvents", reinterpret_cast<void*>(&bionic_AInputQueue_hasEvents)},

    // Input/Motion Symbols
    {"AInputEvent_getType", reinterpret_cast<void*>(&bionic_AInputEvent_getType)},
    {"AInputEvent_getSource", reinterpret_cast<void*>(&bionic_AInputEvent_getSource)},
    {"AInputEvent_getFlags", reinterpret_cast<void*>(&bionic_AInputEvent_getFlags)},
    {"AInputEvent_getEventTime", reinterpret_cast<void*>(&bionic_AInputEvent_getEventTime)},
    {"AMotionEvent_getAction", reinterpret_cast<void*>(&bionic_AMotionEvent_getAction)},
    {"AMotionEvent_getX", reinterpret_cast<void*>(&bionic_AMotionEvent_getX)},
    {"AMotionEvent_getY", reinterpret_cast<void*>(&bionic_AMotionEvent_getY)},
    {"AMotionEvent_getPointerCount", reinterpret_cast<void*>(&bionic_AMotionEvent_getPointerCount)},
    {"AMotionEvent_getXByIndex", reinterpret_cast<void*>(&bionic_AMotionEvent_getXByIndex)},
    {"AMotionEvent_getYByIndex", reinterpret_cast<void*>(&bionic_AMotionEvent_getYByIndex)},
    {"AMotionEvent_getPressure", reinterpret_cast<void*>(&bionic_AMotionEvent_getPressure)},
    {"AMotionEvent_getPressureByIndex", reinterpret_cast<void*>(&bionic_AMotionEvent_getPressureByIndex)},
    {"AMotionEvent_getRawX", reinterpret_cast<void*>(&bionic_AMotionEvent_getRawX)},
    {"AMotionEvent_getRawY", reinterpret_cast<void*>(&bionic_AMotionEvent_getRawY)},
    {"AMotionEvent_getDownTime", reinterpret_cast<void*>(&bionic_AMotionEvent_getDownTime)},
    {"AMotionEvent_getEventTime", reinterpret_cast<void*>(&bionic_AMotionEvent_getEventTime)},
    {"AMotionEvent_getHistorySize", reinterpret_cast<void*>(&bionic_AMotionEvent_getHistorySize)},
    {"AMotionEvent_getHistoricalEventTime", reinterpret_cast<void*>(&bionic_AMotionEvent_getHistoricalEventTime)},
    {"AMotionEvent_getHistoricalX", reinterpret_cast<void*>(&bionic_AMotionEvent_getHistoricalX)},
    {"AMotionEvent_getHistoricalY", reinterpret_cast<void*>(&bionic_AMotionEvent_getHistoricalY)},
    {"AMotionEvent_getHistoricalPressure", reinterpret_cast<void*>(&bionic_AMotionEvent_getHistoricalPressure)},

    // KeyEvent Symbols
    {"AKeyEvent_getAction", reinterpret_cast<void*>(&bionic_AKeyEvent_getAction)},
    {"AKeyEvent_getKeyCode", reinterpret_cast<void*>(&bionic_AKeyEvent_getKeyCode)},
    {"AKeyEvent_getScanCode", reinterpret_cast<void*>(&bionic_AKeyEvent_getScanCode)},
    {"AKeyEvent_getMetaState", reinterpret_cast<void*>(&bionic_AKeyEvent_getMetaState)},
    {"AKeyEvent_getRepeatCount", reinterpret_cast<void*>(&bionic_AKeyEvent_getRepeatCount)},
    {"AKeyEvent_getDownTime", reinterpret_cast<void*>(&bionic_AKeyEvent_getDownTime)},
    {"AKeyEvent_getEventTime", reinterpret_cast<void*>(&bionic_AKeyEvent_getEventTime)},
    {"kudroid_inject_touch_event", reinterpret_cast<void*>(&kudroid_inject_touch_event)},
};

} // namespace

const SymbolEntry* get_input_symbols(size_t* count) {
    if (count) {
        *count = sizeof(kInputSymbols) / sizeof(SymbolEntry);
    }
    return kInputSymbols;
}

} // namespace kudroid
