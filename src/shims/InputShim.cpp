#include "kudroid/shims/InputShim.h"
#include <cstdint>
#include <unistd.h>
namespace kudroid {
namespace {

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
extern "C" ssize_t bionic_ASensorEventQueue_getEvents(void* queue, void* events, size_t count) { (void)queue; (void)events; (void)count; return 0; }
extern "C" const char* bionic_ASensor_getName(void* sensor) { (void)sensor; return "DummySensor"; }
extern "C" const char* bionic_ASensor_getVendor(void* sensor) { (void)sensor; return "Kudroid"; }
extern "C" int bionic_ASensor_getType(void* sensor) { (void)sensor; return 1; }
extern "C" float bionic_ASensor_getResolution(void* sensor) { (void)sensor; return 1.0f; }
extern "C" int bionic_ASensor_getMinDelay(void* sensor) { (void)sensor; return 10000; }

const SymbolEntry kInputSymbols[] = {
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
};

} // namespace

const SymbolEntry* get_input_symbols(size_t* count) {
    if (count) {
        *count = sizeof(kInputSymbols) / sizeof(SymbolEntry);
    }
    return kInputSymbols;
}

} // namespace kudroid
