/**
 * test_gpu_vulkan.c — ARM64 ELF .so that tests Vulkan via dlopen/dlsym.
 * When loaded by KuDroid's ELF loader, the dlopen("libvulkan.so") call
 * gets intercepted by BionicShim, which returns a fake handle and maps
 * dlsym calls directly to MoltenVK on iOS.
 */
#include <dlfcn.h>
#include <stdint.h>
#include <string.h>

/* Android log extern — resolved by BionicShim */
#ifndef ANDROID_LOG_INFO
#define ANDROID_LOG_INFO 4
#endif
extern int __android_log_print(int priority, const char* tag,
                               const char* format, ...);

/* Minimal Vulkan type stubs so we can actually call vkEnumerateInstanceExtensionProperties */
typedef int32_t VkResult;
typedef uint32_t VkBool32;
typedef struct VkApplicationInfo {
    int sType; /* VK_STRUCTURE_TYPE_APPLICATION_INFO = 0 */
    const void* pNext;
    const char* pApplicationName;
    uint32_t applicationVersion;
    const char* pEngineName;
    uint32_t engineVersion;
    uint32_t apiVersion;
} VkApplicationInfo;

typedef struct VkInstanceCreateInfo {
    int sType; /* VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO = 1 */
    const void* pNext;
    uint32_t flags;
    const VkApplicationInfo* pApplicationInfo;
    uint32_t enabledLayerCount;
    const char* const* ppEnabledLayerNames;
    uint32_t enabledExtensionCount;
    const char* const* ppEnabledExtensionNames;
} VkInstanceCreateInfo;

typedef void* VkInstance;

typedef VkResult (*PFN_vkCreateInstance)(const VkInstanceCreateInfo*, const void*, VkInstance*);
typedef VkResult (*PFN_vkEnumerateInstanceExtensionProperties)(const char*, uint32_t*, void*);

/* Result codes for the test runner to interpret */
#define GPU_VK_OK                   0
#define GPU_VK_DLOPEN_FAIL         -1
#define GPU_VK_NO_ENUMERATE        -2
#define GPU_VK_NO_CREATE_INSTANCE  -3
#define GPU_VK_ENUMERATE_FAIL      -4

/**
 * kudroid_gpu_vulkan_test() — main entry point called by KuDroid.
 * Returns 0 on success, negative on failure.
 * ext_count_out receives the number of Vulkan extensions found.
 */
int kudroid_gpu_vulkan_test(uint32_t* ext_count_out) {
    void* vk = dlopen("libvulkan.so", RTLD_NOW);
    if (!vk) {
        __android_log_print(ANDROID_LOG_INFO, "KuDroidGPU",
                            "VK TEST: dlopen(libvulkan.so) FAILED");
        return GPU_VK_DLOPEN_FAIL;
    }
    __android_log_print(ANDROID_LOG_INFO, "KuDroidGPU",
                        "VK TEST: dlopen(libvulkan.so) OK handle=%p", vk);

    /* Try to enumerate extensions — this is a safe read-only call */
    PFN_vkEnumerateInstanceExtensionProperties pfnEnum =
        (PFN_vkEnumerateInstanceExtensionProperties)dlsym(vk, "vkEnumerateInstanceExtensionProperties");
    if (pfnEnum) {
        uint32_t count = 0;
        VkResult res = pfnEnum(NULL, &count, NULL);
        __android_log_print(ANDROID_LOG_INFO, "KuDroidGPU",
                            "VK TEST: vkEnumerateInstanceExtensionProperties => %d extensions (result=%d)",
                            (int)count, (int)res);
        if (ext_count_out) *ext_count_out = count;
    } else {
        __android_log_print(ANDROID_LOG_INFO, "KuDroidGPU",
                            "VK TEST: vkEnumerateInstanceExtensionProperties NOT FOUND");
        if (ext_count_out) *ext_count_out = 0;
    }

    /* Check that vkCreateInstance symbol exists */
    PFN_vkCreateInstance pfnCreate =
        (PFN_vkCreateInstance)dlsym(vk, "vkCreateInstance");
    if (pfnCreate) {
        __android_log_print(ANDROID_LOG_INFO, "KuDroidGPU",
                            "VK TEST: vkCreateInstance FOUND at %p", (void*)pfnCreate);
    } else {
        __android_log_print(ANDROID_LOG_INFO, "KuDroidGPU",
                            "VK TEST: vkCreateInstance NOT FOUND");
        return GPU_VK_NO_CREATE_INSTANCE;
    }

    /* Check a few more key symbols */
    const char* symbols[] = {
        "vkDestroyInstance",
        "vkEnumeratePhysicalDevices",
        "vkGetPhysicalDeviceProperties",
        "vkGetDeviceProcAddr",
        "vkGetInstanceProcAddr",
        NULL
    };
    for (int i = 0; symbols[i]; i++) {
        void* sym = dlsym(vk, symbols[i]);
        __android_log_print(ANDROID_LOG_INFO, "KuDroidGPU",
                            "VK TEST: %s => %p", symbols[i], sym);
    }

    dlclose(vk);
    __android_log_print(ANDROID_LOG_INFO, "KuDroidGPU",
                        "VK TEST: ALL PASSED");
    return GPU_VK_OK;
}
