/**
 * test_gpu_vulkan.c — ARM64 ELF .so that tests Vulkan via dlopen/dlsym.
 * When loaded by KuDroid's ELF loader, the dlopen("libvulkan.so") call
 * gets intercepted by BionicShim, which returns a fake handle and maps
 * dlsym calls directly to MoltenVK on iOS.
 */
#include <dlfcn.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

/* Android log extern — resolved by BionicShim */
#ifndef ANDROID_LOG_INFO
#define ANDROID_LOG_INFO 4
#endif
extern int __android_log_print(int priority, const char* tag,
                               const char* format, ...);

#define VK_SUCCESS 0

typedef uint32_t VkResult;
typedef uint32_t VkStructureType;
typedef void* VkInstance;
typedef void* VkPhysicalDevice;
typedef void* VkDevice;
typedef void* VkQueue;

#define VK_STRUCTURE_TYPE_APPLICATION_INFO 0
#define VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO 1
#define VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO 2
#define VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO 3
#define VK_API_VERSION_1_0 (1 << 22)
#define VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR 0x00000001

typedef struct VkApplicationInfo {
    VkStructureType    sType;
    const void*        pNext;
    const char*        pApplicationName;
    uint32_t           applicationVersion;
    const char*        pEngineName;
    uint32_t           engineVersion;
    uint32_t           apiVersion;
} VkApplicationInfo;

typedef struct VkInstanceCreateInfo {
    VkStructureType             sType;
    const void*                 pNext;
    uint32_t                    flags;
    const VkApplicationInfo*    pApplicationInfo;
    uint32_t                    enabledLayerCount;
    const char* const*          ppEnabledLayerNames;
    uint32_t                    enabledExtensionCount;
    const char* const*          ppEnabledExtensionNames;
} VkInstanceCreateInfo;

typedef struct VkPhysicalDeviceProperties {
    uint32_t        apiVersion;
    uint32_t        driverVersion;
    uint32_t        vendorID;
    uint32_t        deviceID;
    uint32_t        deviceType;
    char            deviceName[256];
    uint8_t         pipelineCacheUUID[16];
    uint32_t        limits[512]; // padding for Vulkan limits
    uint32_t        sparseProperties[64]; // padding for sparse properties
} VkPhysicalDeviceProperties;

typedef struct VkQueueFamilyProperties {
    uint32_t    queueFlags;
    uint32_t    queueCount;
    uint32_t    timestampValidBits;
    uint32_t    minImageTransferGranularity[3];
} VkQueueFamilyProperties;

typedef struct VkDeviceQueueCreateInfo {
    VkStructureType             sType;
    const void*                 pNext;
    uint32_t                    flags;
    uint32_t                    queueFamilyIndex;
    uint32_t                    queueCount;
    const float*                pQueuePriorities;
} VkDeviceQueueCreateInfo;

typedef struct VkDeviceCreateInfo {
    VkStructureType                    sType;
    const void*                        pNext;
    uint32_t                           flags;
    uint32_t                           queueCreateInfoCount;
    const VkDeviceQueueCreateInfo*     pQueueCreateInfos;
    uint32_t                           enabledLayerCount;
    const char* const*                 ppEnabledLayerNames;
    uint32_t                           enabledExtensionCount;
    const char* const*                 ppEnabledExtensionNames;
    const void*                        pEnabledFeatures;
} VkDeviceCreateInfo;

typedef VkResult (*PFN_vkCreateInstance)(const VkInstanceCreateInfo*, const void*, VkInstance*);
typedef void (*PFN_vkDestroyInstance)(VkInstance, const void*);
typedef VkResult (*PFN_vkEnumeratePhysicalDevices)(VkInstance, uint32_t*, VkPhysicalDevice*);
typedef void (*PFN_vkGetPhysicalDeviceProperties)(VkPhysicalDevice, VkPhysicalDeviceProperties*);
typedef void (*PFN_vkGetPhysicalDeviceQueueFamilyProperties)(VkPhysicalDevice, uint32_t*, VkQueueFamilyProperties*);
typedef VkResult (*PFN_vkCreateDevice)(VkPhysicalDevice, const VkDeviceCreateInfo*, const void*, VkDevice*);
typedef void (*PFN_vkDestroyDevice)(VkDevice, const void*);
typedef void (*PFN_vkGetDeviceQueue)(VkDevice, uint32_t, uint32_t, VkQueue*);

/* Result codes */
#define GPU_VK_OK                   0
#define GPU_VK_DLOPEN_FAIL         -1

int kudroid_gpu_vulkan_test(uint32_t* ext_count_out) {
    if (ext_count_out) *ext_count_out = 0;
    
    void* vk = dlopen("libvulkan.so", RTLD_NOW);
    if (!vk) {
        __android_log_print(ANDROID_LOG_INFO, "KuDroidGPU", "VK TEST: dlopen(libvulkan.so) FAILED");
        return GPU_VK_DLOPEN_FAIL;
    }

    PFN_vkCreateInstance vkCreateInstance = (PFN_vkCreateInstance)dlsym(vk, "vkCreateInstance");
    PFN_vkDestroyInstance vkDestroyInstance = (PFN_vkDestroyInstance)dlsym(vk, "vkDestroyInstance");
    PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices = (PFN_vkEnumeratePhysicalDevices)dlsym(vk, "vkEnumeratePhysicalDevices");
    PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties = (PFN_vkGetPhysicalDeviceProperties)dlsym(vk, "vkGetPhysicalDeviceProperties");
    PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties = (PFN_vkGetPhysicalDeviceQueueFamilyProperties)dlsym(vk, "vkGetPhysicalDeviceQueueFamilyProperties");
    PFN_vkCreateDevice vkCreateDevice = (PFN_vkCreateDevice)dlsym(vk, "vkCreateDevice");
    PFN_vkDestroyDevice vkDestroyDevice = (PFN_vkDestroyDevice)dlsym(vk, "vkDestroyDevice");
    PFN_vkGetDeviceQueue vkGetDeviceQueue = (PFN_vkGetDeviceQueue)dlsym(vk, "vkGetDeviceQueue");

    if (!vkCreateInstance || !vkEnumeratePhysicalDevices || !vkCreateDevice) {
        __android_log_print(ANDROID_LOG_INFO, "KuDroidGPU", "VK TEST: Missing core Vulkan functions!");
        return -2;
    }

    VkApplicationInfo appInfo = {0};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "KuDroid Headless VK";
    appInfo.applicationVersion = 1;
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = 1;
    appInfo.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo createInfo = {0};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    
    // MoltenVK on iOS does NOT need the portability enumeration bit/extension.
    // Enabling it causes vkCreateInstance to fail. Keep flags=0 and no extensions.
    createInfo.enabledExtensionCount = 0;
    createInfo.ppEnabledExtensionNames = NULL;
    createInfo.flags = 0;

    VkInstance instance;
    if (vkCreateInstance(&createInfo, NULL, &instance) != VK_SUCCESS) {
        __android_log_print(ANDROID_LOG_INFO, "KuDroidGPU", "VK TEST: vkCreateInstance failed.");
        return -3;
    }
    __android_log_print(ANDROID_LOG_INFO, "KuDroidGPU", "VK TEST: vkCreateInstance OK.");

    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, NULL);
    if (deviceCount == 0) {
        __android_log_print(ANDROID_LOG_INFO, "KuDroidGPU", "VK TEST: Failed to find GPUs with Vulkan support!");
        return -4;
    }
    __android_log_print(ANDROID_LOG_INFO, "KuDroidGPU", "VK TEST: Found %u Physical Devices.", deviceCount);

    VkPhysicalDevice* physicalDevices = (VkPhysicalDevice*)malloc(sizeof(VkPhysicalDevice) * deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, physicalDevices);

    VkPhysicalDevice physicalDevice = physicalDevices[0];
    
    VkPhysicalDeviceProperties deviceProperties = {0};
    if (vkGetPhysicalDeviceProperties) {
        vkGetPhysicalDeviceProperties(physicalDevice, &deviceProperties);
        __android_log_print(ANDROID_LOG_INFO, "KuDroidGPU", "VK TEST: Device 0: %s", deviceProperties.deviceName);
    }

    uint32_t queueFamilyCount = 0;
    if (vkGetPhysicalDeviceQueueFamilyProperties) {
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, NULL);
    }
    if (queueFamilyCount == 0) {
         __android_log_print(ANDROID_LOG_INFO, "KuDroidGPU", "VK TEST: No queue families found!");
         return -5;
    }

    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo = {0};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = 0; // Just use 0
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    // MoltenVK on iOS does NOT need VK_KHR_portability_subset. Requesting it
    // can fail device creation. Keep no device extensions.
    VkDeviceCreateInfo deviceCreateInfo = {0};
    deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
    deviceCreateInfo.queueCreateInfoCount = 1;
    deviceCreateInfo.enabledExtensionCount = 0;
    deviceCreateInfo.ppEnabledExtensionNames = NULL;

    VkDevice device;
    if (vkCreateDevice(physicalDevice, &deviceCreateInfo, NULL, &device) != VK_SUCCESS) {
        __android_log_print(ANDROID_LOG_INFO, "KuDroidGPU", "VK TEST: vkCreateDevice failed.");
        return -6;
    }
    __android_log_print(ANDROID_LOG_INFO, "KuDroidGPU", "VK TEST: vkCreateDevice OK.");
    if (vkGetDeviceQueue) {
        VkQueue queue;
        vkGetDeviceQueue(device, 0, 0, &queue);
        __android_log_print(ANDROID_LOG_INFO, "KuDroidGPU", "VK TEST: vkGetDeviceQueue OK.");
    }
    
    if (device && vkDestroyDevice) {
        vkDestroyDevice(device, NULL);
    }

    if (vkDestroyInstance) vkDestroyInstance(instance, NULL);
    free(physicalDevices);

    __android_log_print(ANDROID_LOG_INFO, "KuDroidGPU", "VK TEST: VK resources cleaned up successfully.");

    return GPU_VK_OK;
}
