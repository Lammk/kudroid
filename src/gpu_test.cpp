#include "kudroid/BionicShim.h"
#include <string>
#include <cstring>
#include <cstdint>

extern "C" const char* kudroid_test_gpu(void) {
    std::string log;
    log += "[kudroid_gpu] Starting Native GPU (OpenGL/Vulkan) Direct Intercept Test...\n";

    // Test Vulkan Direct Intercept
    log += "[kudroid_gpu] Calling bionic_dlopen(\"libvulkan.so\")...\n";
    void* vkHandle = bionic_dlopen("libvulkan.so", 0);
    if (vkHandle) {
        log += "[kudroid_gpu] SUCCESS: Got fake handle for libvulkan.so: " + std::to_string(reinterpret_cast<uintptr_t>(vkHandle)) + "\n";
        void* vkCreateInstance = bionic_dlsym(vkHandle, "vkCreateInstance");
        if (vkCreateInstance) {
            log += "[kudroid_gpu] SUCCESS: Resolved vkCreateInstance directly to iOS native address: " + std::to_string(reinterpret_cast<uintptr_t>(vkCreateInstance)) + "\n";
        } else {
            log += "[kudroid_gpu] WARNING: vkCreateInstance not found via iOS dlsym (MoltenVK not linked?).\n";
        }
    } else {
        log += "[kudroid_gpu] ERROR: bionic_dlopen returned null for libvulkan.so\n";
    }

    // Test OpenGL ES Direct Intercept
    log += "[kudroid_gpu] Calling bionic_dlopen(\"libGLESv2.so\")...\n";
    void* glesHandle = bionic_dlopen("libGLESv2.so", 0);
    if (glesHandle) {
        log += "[kudroid_gpu] SUCCESS: Got fake handle for libGLESv2.so: " + std::to_string(reinterpret_cast<uintptr_t>(glesHandle)) + "\n";
        void* glGetString = bionic_dlsym(glesHandle, "glGetString");
        if (glGetString) {
            log += "[kudroid_gpu] SUCCESS: Resolved glGetString directly to iOS native address: " + std::to_string(reinterpret_cast<uintptr_t>(glGetString)) + "\n";
        } else {
            log += "[kudroid_gpu] WARNING: glGetString not found via iOS dlsym (ANGLE not linked?).\n";
        }
    } else {
        log += "[kudroid_gpu] ERROR: bionic_dlopen returned null for libGLESv2.so\n";
    }

    log += "[kudroid_gpu] GPU Direct Intercept Test Completed.\n";
    return strdup(log.c_str());
}
