#include "kudroid/platform/InputShim.h"
#include "kudroid/platform/NativeTouchGate.h"
#include <cstdint>
#include <new>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <atomic>
#include <mutex>
#include <deque>
#include <vector>
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
constexpr size_t kMaxPointers = 10;

struct PointerCoord {
    float x = 0.0f;
    float y = 0.0f;
    int32_t id = 0;
};

struct BionicInputEvent {
    int32_t type;   // 2 = AINPUT_EVENT_TYPE_MOTION
    int32_t action;
    float x;
    float y;
    int64_t eventTime;
    int32_t pointerCount;
    int32_t source;
    int32_t flags;
    PointerCoord pointers[kMaxPointers];
    uint64_t seq = 0;  // queue sequence; matches copies back to deque entries
};

// Tracks active fingers across events so getX/getY can query any finger by index.
struct PointerSlot {
    bool active = false;
    int32_t id = 0;
    float x = 0.0f;
    float y = 0.0f;
};
static PointerSlot g_activePointers[kMaxPointers];

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
    // getEvent hands the guest a heap copy, never interior storage: any
    // push_back/tail-replace/pop reallocates the deque, dangling a handed-out
    // &front() and turning every getter into a UAF (plus finishEvent's
    // address compare then never matches, redelivering one stale event
    // forever). Copies are matched back by sequence number; at most one is
    // outstanding per the AInputQueue contract, and a replacement getEvent
    // deletes an abandoned copy so leaks cannot accumulate.
    BionicInputEvent* activeCopy = nullptr;
    uint64_t nextSeq = 1;
    std::atomic<int32_t> id; // looper ident; zero until a native looper attaches
    int wakePipe[2]; // pipe to wake the looper when events arrive
    bool pipeReady;

    BionicInputQueue() : id(0), pipeReady(false) {
        wakePipe[0] = -1;
        wakePipe[1] = -1;
    }
};

static BionicInputQueue g_inputQueue;

// Ensure the wake pipe exists (lazily created on first use).
// Internal locks: kudroid_inject_touch_event (Swift thread) and attachLooper
// (game threads) can run concurrently — without a lock, pipe() runs twice,
// leak a pair of fd.
static void ensure_wake_pipe(BionicInputQueue* q) {
    std::lock_guard<std::mutex> lock(q->mtx);
    if (q->pipeReady) return;
    if (::pipe(q->wakePipe) == 0) {
        // Set both read and write ends non-blocking.
        int flags0 = ::fcntl(q->wakePipe[0], F_GETFL, 0);
        ::fcntl(q->wakePipe[0], F_SETFL, flags0 | O_NONBLOCK);
        int flags1 = ::fcntl(q->wakePipe[1], F_GETFL, 0);
        ::fcntl(q->wakePipe[1], F_SETFL, flags1 | O_NONBLOCK);
        q->pipeReady = true;
    }
}

// Exported for kudroid_bridge: AInputQueue pointer used to pass in
// ANativeActivity's onInputQueueCreated callback.
extern "C" void* kudroid_get_input_queue(void) {
    return &g_inputQueue;
}

#include "kudroid/KuArtRuntime.h"

// The gate exists to keep a drag from costing one VM-locked interpreted dispatch
// per 60-120 Hz MOVE (see NativeTouchGate.cpp). It must also only run while a
// runtime is live: kuart_post_touch_event queues regardless, but a queue pushed
// before kuart_launch_app resets it with accepting_=false — those events were
// silently dropped after the log said they were dispatched.
static void forward_touch_to_java_activity(int action, float x, float y, int pointerCount) {
    if (kuart_is_ready() != 1) return;
    // When a native looper is attached, the NDK AInputQueue path already delivers this
    // event; forwarding it again would dispatch it to Java twice, and every MOVE would
    // take the VM lock against the render loop. The Java forward exists for titles that
    // never attach the native queue (the common Unity case).
    if (g_inputQueue.id.load() != 0) return;
    const bool is_move = (action & 0xff) == 2;  // ACTION_MOVE
    if (is_move && kudroid_touch_source_gate_allow_move() != 1) return;
    // Native enqueue only; the consumer handles VM access and session validity.
    kuart_post_touch_event(action, x, y, pointerCount);
}

// Exported for Swift to inject touch events
extern "C" void kudroid_inject_touch_event_multi(float x, float y, int32_t action, int32_t pointerId, int32_t pointerCount) {
    const int32_t baseAction = action & 0xff;

    BionicInputEvent ev;
    ev.type = 2; // AINPUT_EVENT_TYPE_MOTION
    ev.action = action;
    ev.x = x;
    ev.y = y;
    ev.eventTime = static_cast<int64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    ev.pointerCount = pointerCount > 0 ? pointerCount : 1;
    if (ev.pointerCount > static_cast<int32_t>(kMaxPointers)) {
        ev.pointerCount = static_cast<int32_t>(kMaxPointers);
    }
    ev.source = 0x0002; // AINPUT_SOURCE_TOUCHSCREEN
    ev.flags = 0;

    int32_t finalAction = action;

    {
        std::lock_guard<std::mutex> lock(g_inputQueue.mtx);

        // Update active pointer tracking table
        if (baseAction == 0 || baseAction == 5) {
            // DOWN or POINTER_DOWN
            int freeSlot = -1;
            int foundSlot = -1;
            for (size_t i = 0; i < kMaxPointers; ++i) {
                if (g_activePointers[i].active && g_activePointers[i].id == pointerId) {
                    foundSlot = static_cast<int>(i);
                    break;
                }
                if (!g_activePointers[i].active && freeSlot == -1) {
                    freeSlot = static_cast<int>(i);
                }
            }
            int slot = (foundSlot != -1) ? foundSlot : freeSlot;
            if (slot != -1) {
                g_activePointers[slot].active = true;
                g_activePointers[slot].id = pointerId;
                g_activePointers[slot].x = x;
                g_activePointers[slot].y = y;
            }
        } else if (baseAction == 2) {
            // MOVE: update position of moving pointer
            for (size_t i = 0; i < kMaxPointers; ++i) {
                if (g_activePointers[i].active && g_activePointers[i].id == pointerId) {
                    g_activePointers[i].x = x;
                    g_activePointers[i].y = y;
                    break;
                }
            }
        }

        // Copy all currently active pointers into this event
        int outIndex = 0;
        int actionPointerIndex = 0;
        for (size_t i = 0; i < kMaxPointers && outIndex < ev.pointerCount; ++i) {
            if (g_activePointers[i].active) {
                if (g_activePointers[i].id == pointerId) {
                    actionPointerIndex = outIndex;
                }
                ev.pointers[outIndex].id = g_activePointers[i].id;
                ev.pointers[outIndex].x = g_activePointers[i].x;
                ev.pointers[outIndex].y = g_activePointers[i].y;
                ++outIndex;
            }
        }
        // If tracking table had fewer slots than reported pointerCount, fill with primary coords
        while (outIndex < ev.pointerCount) {
            if (outIndex == pointerId) {
                actionPointerIndex = outIndex;
            }
            ev.pointers[outIndex].id = outIndex;
            ev.pointers[outIndex].x = x;
            ev.pointers[outIndex].y = y;
            ++outIndex;
        }

        // Encode actionIndex into finalAction for multi-touch (pointerIndex << 8) | baseAction
        if (pointerId > 0) {
            if (baseAction == 0) {
                finalAction = (actionPointerIndex << 8) | 5; // ACTION_POINTER_DOWN
            } else if (baseAction == 1) {
                finalAction = (actionPointerIndex << 8) | 6; // ACTION_POINTER_UP
            }
        }
        ev.action = finalAction;

        // Handle pointer removal on UP / POINTER_UP / CANCEL
        if (baseAction == 1 || baseAction == 3) {
            // UP or CANCEL: all fingers released
            for (size_t i = 0; i < kMaxPointers; ++i) {
                g_activePointers[i].active = false;
            }
        } else if (baseAction == 6) {
            // POINTER_UP: remove specific pointer
            for (size_t i = 0; i < kMaxPointers; ++i) {
                if (g_activePointers[i].active && g_activePointers[i].id == pointerId) {
                    g_activePointers[i].active = false;
                    break;
                }
            }
        }

        // Coalesce MOVE floods in the NDK queue: replace the pending MOVE instead
        // of enqueuing another event per touch. Unity drains the queue every frame
        // and interpolates; intermediate positions are dead weight. DOWN/UP/CANCEL
        // always enqueue — ordering there is semantic.
        const bool is_move = (finalAction & 0xff) == 2;  // ACTION_MOVE
        bool replaced = false;
        if (is_move && !g_inputQueue.events.empty()) {
            auto& tail = g_inputQueue.events.back();
            if (tail.type == 2 && (tail.action & 0xff) == 2) {
                tail = ev;
                replaced = true;
            }
        }
        if (!replaced) {
            ev.seq = g_inputQueue.nextSeq++;  // under q->mtx here
            g_inputQueue.events.push_back(ev);
        }
    }

    // Wake the looper only if a native looper is attached and polling the queue.
    if (g_inputQueue.id.load() != 0) {
        ensure_wake_pipe(&g_inputQueue);
        if (g_inputQueue.pipeReady) {
            uint8_t byte = 1;
            ssize_t unused = ::write(g_inputQueue.wakePipe[1], &byte, 1);
            (void)unused;
        }
    }

    // Forward touch events to Java Activity (e.g. Unity uGUI buttons like "Skip Tutorial").
    // Non-move events (DOWN, UP, CANCEL) are always forwarded to ensure UI clicks trigger.
    // MOVE events are rate-gated by NativeTouchGate to avoid stalling the render loop.
    forward_touch_to_java_activity(finalAction, x, y, ev.pointerCount);
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
    // Heap copy, never &front(): see the struct comment. An abandoned previous
    // copy is deleted here; its event stays queued until finished by seq.
    delete q->activeCopy;
    q->activeCopy = new (std::nothrow) BionicInputEvent(q->events.front());
    *outEvent = q->activeCopy;
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
    auto* copy = static_cast<BionicInputEvent*>(event);
    // Pop only when the copy still names the queued front: a stale/double
    // finish, or a finish after the queue moved on, must not pop a live event.
    if (copy == q->activeCopy && !q->events.empty() &&
        q->events.front().seq == copy->seq) {
        q->events.pop_front();
    }
    if (copy == q->activeCopy) {
        delete q->activeCopy;
        q->activeCopy = nullptr;
    }
    return 0;
}

extern "C" void bionic_AInputQueue_attachLooper(void* queue, void* looper, int ident,
                                                void* callback, void* data) {
    if (!queue) return;
    BionicInputQueue* q = static_cast<BionicInputQueue*>(queue);
    q->id.store(ident);

    // Register the wake pipe with the looper so poll() wakes on input.
    ensure_wake_pipe(q);
    if (q->pipeReady && looper) {
        bionic_ALooper_addFd(looper, q->wakePipe[0], ident, 0x0001 /* ALOOPER_EVENT_INPUT */,
                             callback, data);
        // Mark this fd as wake pipe of AInputQueue to ALooper_pollAll drain
        // water every time it is readable — otherwise the pipe will always be ready -> busy loop.
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
    if (!event) return 0.0f;
    const auto* ev = static_cast<const BionicInputEvent*>(event);
    if (pointer_index < static_cast<size_t>(ev->pointerCount) && pointer_index < kMaxPointers) {
        return ev->pointers[pointer_index].x;
    }
    return ev->x;
}

extern "C" float bionic_AMotionEvent_getY(const void* event, size_t pointer_index) {
    if (!event) return 0.0f;
    const auto* ev = static_cast<const BionicInputEvent*>(event);
    if (pointer_index < static_cast<size_t>(ev->pointerCount) && pointer_index < kMaxPointers) {
        return ev->pointers[pointer_index].y;
    }
    return ev->y;
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
    if (!event) return static_cast<int32_t>(pointer_index);
    const auto* ev = static_cast<const BionicInputEvent*>(event);
    if (pointer_index < static_cast<size_t>(ev->pointerCount) && pointer_index < kMaxPointers) {
        return ev->pointers[pointer_index].id;
    }
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
// Bionic ASensorManager shims (Real sensor bridge from iOS CoreMotion)
// ─────────────────────────────────────────────────────────────────────────────

struct ASensorVector {
    float x;
    float y;
    float z;
    int8_t status;
    uint8_t reserved[3];
};

struct ASensorEvent {
    int32_t version;
    int32_t sensor;
    int32_t type;
    int32_t reserved0;
    int64_t timestamp;
    union {
        float data[16];
        ASensorVector vector;
        ASensorVector acceleration;
    };
    int32_t reserved1[4];
};

struct DummySensor {
    int type;
    const char* name;
    const char* vendor;
};

static DummySensor g_sensorAccel = {1, "iOS Accelerometer", "Apple"};
static DummySensor g_sensorGyro = {4, "iOS Gyroscope", "Apple"};
static DummySensor g_sensorOrient = {3, "iOS Orientation Sensor", "Apple"};
static DummySensor* g_sensorList[3] = {&g_sensorAccel, &g_sensorGyro, &g_sensorOrient};

static std::mutex g_sensorMutex;
static std::vector<ASensorEvent> g_sensorEventQueue;

extern "C" void kudroid_inject_sensor_event(int sensorType, float x, float y, float z) {
    ASensorEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.version = sizeof(ASensorEvent);
    ev.sensor = sensorType;
    ev.type = sensorType;
    ev.timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    ev.acceleration.x = x;
    ev.acceleration.y = y;
    ev.acceleration.z = z;
    ev.acceleration.status = 3; // SENSOR_STATUS_ACCURACY_HIGH

    {
        std::lock_guard<std::mutex> lock(g_sensorMutex);
        if (g_sensorEventQueue.size() < 64) {
            g_sensorEventQueue.push_back(ev);
        }
    }
}

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
    (void)manager;
    if (list) {
        *list = g_sensorList;
    }
    return 3;
}

extern "C" void* bionic_ASensorManager_getDefaultSensor(void* manager, int type) {
    (void)manager;
    if (type == 1) return &g_sensorAccel;
    if (type == 4) return &g_sensorGyro;
    if (type == 3) return &g_sensorOrient;
    // Unknown sensor: NULL, not the accelerometer. Returning a sensor that is
    // not there makes the guest configure and wait on data that never comes.
    return nullptr;
}

extern "C" int bionic_ASensorEventQueue_enableSensor(void* queue, void* sensor) { (void)queue; (void)sensor; return 0; }
extern "C" int bionic_ASensorEventQueue_disableSensor(void* queue, void* sensor) { (void)queue; (void)sensor; return 0; }
extern "C" int bionic_ASensorEventQueue_setEventRate(void* queue, void* sensor, int32_t usec) { (void)queue; (void)sensor; (void)usec; return 0; }
extern "C" int bionic_ASensorEventQueue_hasEvents(void* queue) {
    (void)queue;
    std::lock_guard<std::mutex> lock(g_sensorMutex);
    return g_sensorEventQueue.empty() ? 0 : 1;
}

extern "C" ssize_t bionic_ASensorEventQueue_getEvents(void* queue, void* events, size_t count) {
    (void)queue;
    if (!events || count == 0) return 0;
    // The guest declares its ASensorEvent layout by sizeof: a size mismatch
    // here is a buffer overflow on their side, so serve nothing instead.
    // NDK sizeof(ASensorEvent) is 104.
    if (count > 1024) count = 1024;
    std::lock_guard<std::mutex> lock(g_sensorMutex);
    size_t num = std::min(count, g_sensorEventQueue.size());
    if (num > 0) {
        ASensorEvent* dst = static_cast<ASensorEvent*>(events);
        for (size_t i = 0; i < num; ++i) {
            dst[i] = g_sensorEventQueue[i];
        }
        g_sensorEventQueue.erase(g_sensorEventQueue.begin(), g_sensorEventQueue.begin() + num);
    }
    return num;
}

extern "C" const char* bionic_ASensor_getName(void* sensor) {
    if (!sensor) return "Unknown";
    return static_cast<DummySensor*>(sensor)->name;
}
extern "C" const char* bionic_ASensor_getVendor(void* sensor) {
    if (!sensor) return "Apple";
    return static_cast<DummySensor*>(sensor)->vendor;
}
extern "C" int bionic_ASensor_getType(void* sensor) {
    if (!sensor) return 1;
    return static_cast<DummySensor*>(sensor)->type;
}
extern "C" float bionic_ASensor_getResolution(void* sensor) { (void)sensor; return 0.001f; }
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
    {"AMotionEvent_getPointerId", reinterpret_cast<void*>(&bionic_AMotionEvent_getPointerId)},
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
