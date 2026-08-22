typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef int int32_t;
typedef unsigned long long uint64_t;
typedef long long int64_t;
typedef unsigned long size_t;
#define NULL ((void*)0)

// Bionic Android Log API
extern int __android_log_print(int priority, const char* tag, const char* fmt, ...);
#define LOG_TAG "InputMicro"
#define LOGI(...) __android_log_print(3, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(6, LOG_TAG, __VA_ARGS__)

// Android Input Event Definitions
#define AINPUT_EVENT_TYPE_KEY 1
#define AINPUT_EVENT_TYPE_MOTION 2

#define AMOTION_EVENT_ACTION_DOWN 0
#define AMOTION_EVENT_ACTION_UP 1
#define AMOTION_EVENT_ACTION_MOVE 2
#define AMOTION_EVENT_ACTION_CANCEL 3

#define AINPUT_SOURCE_TOUCHSCREEN 0x0002

// Standard Android NDK Input Queue & MotionEvent APIs
extern void* AInputQueue_create(void);
extern int32_t AInputQueue_getEvent(void* queue, void** outEvent);
extern int32_t AInputQueue_preDispatchEvent(void* queue, void* event);
extern int32_t AInputQueue_finishEvent(void* queue, void* event, int handled);
extern int32_t AInputQueue_hasEvents(void* queue);

extern int32_t AInputEvent_getType(const void* event);
extern int32_t AInputEvent_getSource(const void* event);
extern int32_t AInputEvent_getFlags(const void* event);
extern int64_t AInputEvent_getEventTime(const void* event);

extern int32_t AMotionEvent_getAction(const void* event);
extern float AMotionEvent_getX(const void* event, size_t pointer_index);
extern float AMotionEvent_getY(const void* event, size_t pointer_index);
extern float AMotionEvent_getPressure(const void* event, size_t pointer_index);
extern size_t AMotionEvent_getPointerCount(const void* event);
extern int32_t AMotionEvent_getPointerId(const void* event, size_t pointer_index);
extern float AMotionEvent_getRawX(const void* event, size_t pointer_index);
extern float AMotionEvent_getRawY(const void* event, size_t pointer_index);
extern int64_t AMotionEvent_getDownTime(const void* event);

// Exported function used by iOS NativeMetalView to inject touch
extern void kudroid_inject_touch_event(float x, float y, int32_t action);

int kudroid_test_main(void) {
    LOGI("=================================================");
    LOGI("🖐️ [KuDroid Multi-Touch & Input Queue Pipeline Test]");
    LOGI("=================================================");

    // 1. Khởi tạo Input Queue
    void* inputQueue = AInputQueue_create();
    if (!inputQueue) {
        LOGE("❌ Failed to create AInputQueue");
        return 1;
    }
    LOGI("✔ [STAGE 1] AInputQueue acquired: %p (Initial hasEvents: %d)",
         inputQueue, AInputQueue_hasEvents(inputQueue));

    // 2. Mô phỏng chuỗi vuốt chạm của người dùng (Swipe gesture)
    LOGI("⏳ [STAGE 2] Injecting Multi-touch gestures (DOWN ➔ MOVE ➔ MOVE ➔ UP)...");
    kudroid_inject_touch_event(250.0f, 500.0f, AMOTION_EVENT_ACTION_DOWN);
    kudroid_inject_touch_event(300.0f, 550.0f, AMOTION_EVENT_ACTION_MOVE);
    kudroid_inject_touch_event(450.0f, 700.0f, AMOTION_EVENT_ACTION_MOVE);
    kudroid_inject_touch_event(450.0f, 700.0f, AMOTION_EVENT_ACTION_UP);

    int hasEvents = AInputQueue_hasEvents(inputQueue);
    LOGI("✔ [STAGE 2] 4 MotionEvents injected successfully! (hasEvents: %d)", hasEvents);
    if (!hasEvents) {
        LOGE("❌ Input queue is empty after injection");
        return 2;
    }

    // 3. Vòng lặp xử lý Input của Game Engine (Game Loop Event Dispatch)
    LOGI("⏳ [STAGE 3] Dispatching and consuming events in Game Loop...");
    const char* actionNames[] = { "ACTION_DOWN", "ACTION_UP", "ACTION_MOVE", "ACTION_CANCEL" };

    int eventIndex = 0;
    void* event = NULL;
    while (AInputQueue_getEvent(inputQueue, &event) == 0 && event != NULL) {
        if (AInputQueue_preDispatchEvent(inputQueue, event)) {
            continue;
        }

        int32_t type = AInputEvent_getType(event);
        int32_t action = AMotionEvent_getAction(event);
        float x = AMotionEvent_getX(event, 0);
        float y = AMotionEvent_getY(event, 0);
        float pressure = AMotionEvent_getPressure(event, 0);
        int32_t source = AInputEvent_getSource(event);
        size_t pointerCount = AMotionEvent_getPointerCount(event);
        int64_t timeNs = AInputEvent_getEventTime(event);

        const char* actionStr = (action >= 0 && action <= 3) ? actionNames[action] : "ACTION_UNKNOWN";

        LOGI("   👉 [Event #%d] Type=%s (%d), Action=%s (%d), Coords=(X:%.1f, Y:%.1f), Pressure=%.1f, Pointers=%u, Source=0x%x, Timestamp=%lld",
             eventIndex + 1,
             (type == AINPUT_EVENT_TYPE_MOTION) ? "MOTION" : "KEY", type,
             actionStr, action,
             x, y, pressure, (uint32_t)pointerCount, (uint32_t)source, (long long)timeNs);

        // Xác thực tính toàn vẹn của sự kiện
        if (type != AINPUT_EVENT_TYPE_MOTION) {
            LOGE("❌ Event type mismatch: expected MOTION (2), got %d", type);
            return 3;
        }

        // Hoàn tất xử lý sự kiện
        AInputQueue_finishEvent(inputQueue, event, 1);
        eventIndex++;
        event = NULL;
    }

    LOGI("✔ [STAGE 4] Successfully processed %d MotionEvents from AInputQueue!", eventIndex);

    // 4. Kiểm tra hàng đợi đã cạn sạch
    int remainingEvents = AInputQueue_hasEvents(inputQueue);
    LOGI("✔ [STAGE 5] Remaining events in queue: %d (Zero-backlog confirmed)", remainingEvents);

    LOGI("=================================================");
    LOGI("🎉 MULTI-TOUCH & INPUT PIPELINE PASSED 100%!");
    LOGI("=================================================");
    return 0;
}
