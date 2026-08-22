#include <jni.h>
#include <vulkan/vulkan.h>
#include <android/log.h>
#include <dlfcn.h>
#include <stdio.h>
#include <vector>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "TriangleVulkan", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "TriangleVulkan", __VA_ARGS__)

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    LOGI("JNI_OnLoad called for TriangleVulkan!");
    
    // Attempt to load Vulkan and initialize
    void* libVulkan = dlopen("libvulkan.so", RTLD_NOW);
    if (!libVulkan) {
        LOGE("Failed to load libvulkan.so");
        return JNI_VERSION_1_6;
    }
    
    auto vkEnumerateInstanceExtensionProperties = (PFN_vkEnumerateInstanceExtensionProperties) dlsym(libVulkan, "vkEnumerateInstanceExtensionProperties");
    
    if (vkEnumerateInstanceExtensionProperties) {
        uint32_t extCount = 0;
        vkEnumerateInstanceExtensionProperties(nullptr, &extCount, nullptr);
        LOGI("Vulkan Initialized successfully! Found %u instance extensions.", extCount);
        
        if (extCount > 0) {
            std::vector<VkExtensionProperties> extensions(extCount);
            vkEnumerateInstanceExtensionProperties(nullptr, &extCount, extensions.data());
            for (uint32_t i = 0; i < extCount && i < 5; ++i) {
                LOGI(" - %s", extensions[i].extensionName);
            }
        }
    } else {
        LOGE("Could not find vkEnumerateInstanceExtensionProperties");
    }
    
    LOGI("TriangleVulkan initialization complete. Returning JNI_VERSION_1_6.");
    return JNI_VERSION_1_6;
}
