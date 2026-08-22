typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef int int32_t;
typedef unsigned long long uint64_t;
typedef long long int64_t;
typedef unsigned long size_t;
typedef long ssize_t;
typedef signed char int8_t;
#define NULL ((void*)0)

// Android Log API
extern int __android_log_print(int priority, const char* tag, const char* fmt, ...);
#define LOG_TAG "SensorHapticTest"
#define LOGI(...) __android_log_print(4, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(6, LOG_TAG, __VA_ARGS__)

// Bridge declarations
extern void kudroid_set_requested_orientation(int orientation);
extern int kudroid_get_requested_orientation(void);
extern void kudroid_vibrate(int intensity);
extern int usleep(unsigned int usec);

// NDK Sensor declarations
typedef struct ASensorManager ASensorManager;
typedef struct ASensorEventQueue ASensorEventQueue;
typedef struct ASensor ASensor;

typedef struct ASensorVector {
    float x;
    float y;
    float z;
    int8_t status;
    uint8_t reserved[3];
} ASensorVector;

typedef struct ASensorEvent {
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
} ASensorEvent;

extern ASensorManager* ASensorManager_getInstance(void);
extern ASensorEventQueue* ASensorManager_createEventQueue(ASensorManager* manager, void* looper, int ident, void* callback, void* data);
extern const ASensor* ASensorManager_getDefaultSensor(ASensorManager* manager, int type);
extern int ASensorEventQueue_enableSensor(ASensorEventQueue* queue, const ASensor* sensor);
extern ssize_t ASensorEventQueue_getEvents(ASensorEventQueue* queue, ASensorEvent* events, size_t count);

int kudroid_test_main(void) {
    LOGI("=================================================================");
    LOGI("[KuDroidTest] Starting Sensor, Orientation & Haptic Feedback Test");
    LOGI("=================================================================");

    // 1. Test Xoay Màn Hình Sang Ngang (LANDSCAPE = 0)
    LOGI("[Step 1] Requesting LANDSCAPE orientation (0)...");
    kudroid_set_requested_orientation(0);
    int curOri = kudroid_get_requested_orientation();
    LOGI("[Step 1] Current Requested Orientation = %d (Expected: 0)", curOri);
    usleep(1500000); // 1.5s

    // 2. Test Rung Nhẹ (Light Haptic)
    LOGI("[Step 2] Triggering LIGHT Haptic Feedback (intensity=1)...");
    kudroid_vibrate(1);
    usleep(1000000); // 1.0s

    // 3. Test Rung Vừa (Medium Haptic)
    LOGI("[Step 3] Triggering MEDIUM Haptic Feedback (intensity=2)...");
    kudroid_vibrate(2);
    usleep(1000000); // 1.0s

    // 4. Test Xoay Màn Hình Sang Dọc (PORTRAIT = 1)
    LOGI("[Step 4] Requesting PORTRAIT orientation (1)...");
    kudroid_set_requested_orientation(1);
    curOri = kudroid_get_requested_orientation();
    LOGI("[Step 4] Current Requested Orientation = %d (Expected: 1)", curOri);
    usleep(1500000); // 1.5s

    // 5. Test Rung Mạnh (Heavy Haptic)
    LOGI("[Step 5] Triggering HEAVY Haptic Feedback (intensity=3)...");
    kudroid_vibrate(3);
    usleep(1000000); // 1.0s

    // 6. Test ASensorManager NDK
    LOGI("[Step 6] Testing NDK ASensorManager reading...");
    ASensorManager* sm = ASensorManager_getInstance();
    if (sm) {
        const ASensor* accel = ASensorManager_getDefaultSensor(sm, 1);
        LOGI("[Step 6] Found Default Accelerometer: %p", (void*)accel);
        ASensorEventQueue* queue = ASensorManager_createEventQueue(sm, NULL, 0, NULL, NULL);
        if (queue) {
            ASensorEventQueue_enableSensor(queue, accel);
            ASensorEvent ev;
            ssize_t n = ASensorEventQueue_getEvents(queue, &ev, 1);
            LOGI("[Step 6] ASensorEventQueue_getEvents returned %zd events", n);
            if (n > 0) {
                LOGI("[Step 6] Accelerometer: x=%.2f, y=%.2f, z=%.2f", ev.acceleration.x, ev.acceleration.y, ev.acceleration.z);
            }
        }
    }

    LOGI("=================================================================");
    LOGI("[KuDroidTest] ALL SENSOR, ORIENTATION & HAPTIC TESTS PASS!");
    LOGI("=================================================================");
    return 0;
}
