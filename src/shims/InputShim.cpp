#include "kudroid/shims/InputShim.h"
#include <cstdint>
#include <unistd.h>
#include <cstring>
namespace kudroid {
namespace {

// AInputEvent / AMotionEvent simulated structure
struct BionicInputEvent {
    int32_t type; // 2 = AINPUT_EVENT_TYPE_MOTION
    int32_t action;
    float x;
    float y;
};

static BionicInputEvent g_touchEvents[16];
static int g_touchCount = 0;

// Exported for Swift to inject touch events
extern "C" void kudroid_inject_touch_event(float x, float y, int32_t action) {
    if (g_touchCount < 16) {
        g_touchEvents[g_touchCount].type = 2; // AINPUT_EVENT_TYPE_MOTION
        g_touchEvents[g_touchCount].action = action;
        g_touchEvents[g_touchCount].x = x;
        g_touchEvents[g_touchCount].y = y;
        g_touchCount++;
    }
}

// Bionic AMotionEvent shims
extern "C" int32_t bionic_AInputEvent_getType(const void* event) {
    if (!event) return 0;
    return static_cast<const BionicInputEvent*>(event)->type;
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

// Bionic ASensorManager shims (unchanged)
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
    
    // Input/Motion Symbols
    {"AInputEvent_getType", reinterpret_cast<void*>(&bionic_AInputEvent_getType)},
    {"AMotionEvent_getAction", reinterpret_cast<void*>(&bionic_AMotionEvent_getAction)},
    {"AMotionEvent_getX", reinterpret_cast<void*>(&bionic_AMotionEvent_getX)},
    {"AMotionEvent_getY", reinterpret_cast<void*>(&bionic_AMotionEvent_getY)},
};

} // namespace

const SymbolEntry* get_input_symbols(size_t* count) {
    if (count) {
        *count = sizeof(kInputSymbols) / sizeof(SymbolEntry);
    }
    return kInputSymbols;
}

} // namespace kudroid
