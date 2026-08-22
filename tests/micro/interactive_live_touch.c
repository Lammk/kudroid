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
#define LOG_TAG "LiveTouch"
#define LOGI(...) __android_log_print(3, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(5, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(6, LOG_TAG, __VA_ARGS__)

#define AINPUT_EVENT_TYPE_MOTION 2
#define AMOTION_EVENT_ACTION_DOWN 0
#define AMOTION_EVENT_ACTION_UP 1
#define AMOTION_EVENT_ACTION_MOVE 2
#define AMOTION_EVENT_ACTION_CANCEL 3

extern void* AInputQueue_create(void);
extern int32_t AInputQueue_getEvent(void* queue, void** outEvent);
extern int32_t AInputQueue_preDispatchEvent(void* queue, void* event);
extern int32_t AInputQueue_finishEvent(void* queue, void* event, int handled);
extern int32_t AInputQueue_hasEvents(void* queue);

extern int32_t AInputEvent_getType(const void* event);
extern int32_t AMotionEvent_getAction(const void* event);
extern float AMotionEvent_getX(const void* event, size_t pointer_index);
extern float AMotionEvent_getY(const void* event, size_t pointer_index);
extern size_t AMotionEvent_getPointerCount(const void* event);
extern int usleep(unsigned int usec);

int kudroid_test_main(void) {
    LOGI("=================================================");
    LOGI("🖐️ [HARDCORE INTERACTIVE MULTI-TOUCH LIVE LISTENER]");
    LOGI("=================================================");
    LOGI("📱 Canvas is LIVE! You have 12 SECONDS to touch/swipe on iPhone screen...");

    void* inputQueue = AInputQueue_create();
    if (!inputQueue) {
        LOGE("❌ Failed to get AInputQueue");
        return 1;
    }

    const char* actionNames[] = { "👇 ACTION_DOWN", "🖐️ ACTION_UP", "👉 ACTION_MOVE", "❌ ACTION_CANCEL" };

    uint32_t totalEventsReceived = 0;
    uint32_t loopIterations = 1200; // 1200 * 10ms = 12 seconds

    for (uint32_t loop = 0; loop < loopIterations; ++loop) {
        void* event = NULL;
        while (AInputQueue_getEvent(inputQueue, &event) == 0 && event != NULL) {
            if (AInputQueue_preDispatchEvent(inputQueue, event)) {
                continue;
            }

            int32_t type = AInputEvent_getType(event);
            if (type == AINPUT_EVENT_TYPE_MOTION) {
                int32_t action = AMotionEvent_getAction(event);
                float rawX = AMotionEvent_getX(event, 0);
                float rawY = AMotionEvent_getY(event, 0);
                size_t pointerCount = AMotionEvent_getPointerCount(event);

                int32_t intX = (int32_t)rawX;
                int32_t intY = (int32_t)rawY;

                const char* actionStr = (action >= 0 && action <= 3) ? actionNames[action] : "ACTION_OTHER";

                LOGI("🔥 [TOUCH #%u] %s ➔ Pixel: (X=%d, Y=%d) | Active Fingers: %u",
                     totalEventsReceived + 1, actionStr, intX, intY, (uint32_t)pointerCount);

                totalEventsReceived++;
            }

            AInputQueue_finishEvent(inputQueue, event, 1);
            event = NULL;
        }

        usleep(10000); // 10ms polling interval
    }

    LOGI("=================================================");
    if (totalEventsReceived > 0) {
        LOGI("🎉 HARDCORE TEST PASSED! Received %u REAL TOUCH GESTURES from user's finger!", totalEventsReceived);
    } else {
        LOGW("ℹ️ Test finished without touch input (0 gestures detected in 12s).");
    }
    LOGI("=================================================");
    return 0;
}
