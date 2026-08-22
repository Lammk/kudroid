typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef int int32_t;
typedef unsigned long size_t;
#define NULL ((void*)0)

// Android Log
int __android_log_print(int prio, const char* tag, const char* fmt, ...);
#define LOGI(...) __android_log_print(3, "VulkanMicro", __VA_ARGS__)
#define LOGE(...) __android_log_print(6, "VulkanMicro", __VA_ARGS__)

// Vulkan Basic Types & Handles
typedef uint32_t VkResult;
typedef uint32_t VkFlags;
typedef uint32_t VkStructureType;
typedef void* VkInstance;
typedef void* VkPhysicalDevice;
typedef void* VkDevice;
typedef void* VkQueue;
typedef void* VkSurfaceKHR;
typedef void* PFN_vkVoidFunction;

#define VK_SUCCESS 0
#define VK_STRUCTURE_TYPE_APPLICATION_INFO 0
#define VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO 1
#define VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO 2
#define VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO 3
#define VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR 1000008000

typedef struct VkApplicationInfo {
    VkStructureType sType;
    const void* pNext;
    const char* pApplicationName;
    uint32_t applicationVersion;
    const char* pEngineName;
    uint32_t engineVersion;
    uint32_t apiVersion;
} VkApplicationInfo;

typedef struct VkInstanceCreateInfo {
    VkStructureType sType;
    const void* pNext;
    VkFlags flags;
    const VkApplicationInfo* pApplicationInfo;
    uint32_t enabledLayerCount;
    const char* const* ppEnabledLayerNames;
    uint32_t enabledExtensionCount;
    const char* const* ppEnabledExtensionNames;
} VkInstanceCreateInfo;

typedef struct VkExtensionProperties {
    char extensionName[256];
    uint32_t specVersion;
} VkExtensionProperties;

typedef struct VkPhysicalDeviceProperties {
    uint32_t apiVersion;
    uint32_t driverVersion;
    uint32_t vendorID;
    uint32_t deviceID;
    uint32_t deviceType;
    char deviceName[256];
    uint8_t pipelineCacheUUID[16];
    uint32_t limits[128]; // Opaque padding for limits
} VkPhysicalDeviceProperties;

typedef struct VkQueueFamilyProperties {
    VkFlags queueFlags;
    uint32_t queueCount;
    uint32_t timestampValidBits;
    uint32_t minImageTransferGranularity[3];
} VkQueueFamilyProperties;

typedef struct VkDeviceQueueCreateInfo {
    VkStructureType sType;
    const void* pNext;
    VkFlags flags;
    uint32_t queueFamilyIndex;
    uint32_t queueCount;
    const float* pQueuePriorities;
} VkDeviceQueueCreateInfo;

typedef struct VkDeviceCreateInfo {
    VkStructureType sType;
    const void* pNext;
    VkFlags flags;
    uint32_t queueCreateInfoCount;
    const VkDeviceQueueCreateInfo* pQueueCreateInfos;
    uint32_t enabledLayerCount;
    const char* const* ppEnabledLayerNames;
    uint32_t enabledExtensionCount;
    const char* const* ppEnabledExtensionNames;
    const void* pEnabledFeatures;
} VkDeviceCreateInfo;

typedef struct VkAndroidSurfaceCreateInfoKHR {
    VkStructureType sType;
    const void* pNext;
    VkFlags flags;
    void* window;
} VkAndroidSurfaceCreateInfoKHR;

// Entrypoints exported by libvulkan.so / GraphicsShim
PFN_vkVoidFunction vkGetInstanceProcAddr(VkInstance instance, const char* pName);
VkResult vkEnumerateInstanceExtensionProperties(const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties);
VkResult vkCreateAndroidSurfaceKHR(VkInstance instance, const VkAndroidSurfaceCreateInfoKHR* pCreateInfo, const void* pAllocator, VkSurfaceKHR* pSurface);
void* ANativeWindow_fromSurface(void* env, void* surface);

// Function Pointer Typedefs
typedef VkResult (*PFN_vkCreateInstance)(const VkInstanceCreateInfo*, const void*, VkInstance*);
typedef void (*PFN_vkDestroyInstance)(VkInstance, const void*);
typedef VkResult (*PFN_vkEnumeratePhysicalDevices)(VkInstance, uint32_t*, VkPhysicalDevice*);
typedef void (*PFN_vkGetPhysicalDeviceProperties)(VkPhysicalDevice, VkPhysicalDeviceProperties*);
typedef void (*PFN_vkGetPhysicalDeviceQueueFamilyProperties)(VkPhysicalDevice, uint32_t*, VkQueueFamilyProperties*);
typedef VkResult (*PFN_vkCreateDevice)(VkPhysicalDevice, const VkDeviceCreateInfo*, const void*, VkDevice*);
typedef void (*PFN_vkDestroyDevice)(VkDevice, const void*);
typedef void (*PFN_vkDestroySurfaceKHR)(VkInstance, VkSurfaceKHR, const void*);

int kudroid_test_main(void) {
    LOGI("=================================================");
    LOGI("🌋 [Vulkan & MoltenVK Subsystem Diagnostics]");
    LOGI("=================================================");

    // STEP 1: Kiểm tra vkGetInstanceProcAddr
    LOGI("[STEP 1/7] Resolving vkGetInstanceProcAddr...");
    PFN_vkCreateInstance fnCreateInstance =
        (PFN_vkCreateInstance)vkGetInstanceProcAddr(NULL, "vkCreateInstance");
    if (!fnCreateInstance) {
        LOGE("❌ vkGetInstanceProcAddr failed to resolve 'vkCreateInstance'");
        return 1;
    }
    LOGI("✔ [STEP 1] vkCreateInstance resolved at %p", (void*)fnCreateInstance);

    // STEP 2: Liệt kê Instance Extensions
    LOGI("[STEP 2/7] Querying Instance Extension count...");
    uint32_t extCount = 0;
    vkEnumerateInstanceExtensionProperties(NULL, &extCount, NULL);
    LOGI("✔ [STEP 2] Vulkan Instance Extensions count: %u", extCount);

    VkExtensionProperties extProps[32];
    uint32_t queryCount = extCount < 32 ? extCount : 32;
    vkEnumerateInstanceExtensionProperties(NULL, &queryCount, extProps);
    uint32_t previewCount = queryCount < 6 ? queryCount : 6;
    for (uint32_t i = 0; i < previewCount; ++i) {
        LOGI("   - Extension [%u]: %s (spec: %u)", i, extProps[i].extensionName, extProps[i].specVersion);
    }
    if (queryCount > previewCount) {
        LOGI("   ... and %u more extensions", queryCount - previewCount);
    }

    // STEP 3: Khởi tạo Vulkan Instance
    LOGI("[STEP 3/7] Calling vkCreateInstance...");
    VkApplicationInfo appInfo;
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pNext = NULL;
    appInfo.pApplicationName = "KuDroid Vulkan Diagnostic";
    appInfo.applicationVersion = 1;
    appInfo.pEngineName = "KuDroidCore";
    appInfo.engineVersion = 1;
    appInfo.apiVersion = (1 << 22) | (2 << 12); // Vulkan 1.2

    const char* enabledExtensions[] = {
        "VK_KHR_surface",
        "VK_KHR_android_surface"
    };

    VkInstanceCreateInfo instInfo;
    instInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instInfo.pNext = NULL;
    instInfo.flags = 0;
    instInfo.pApplicationInfo = &appInfo;
    instInfo.enabledLayerCount = 0;
    instInfo.ppEnabledLayerNames = NULL;
    instInfo.enabledExtensionCount = 2;
    instInfo.ppEnabledExtensionNames = enabledExtensions;

    VkInstance instance = NULL;
    VkResult res = fnCreateInstance(&instInfo, NULL, &instance);
    if (res != VK_SUCCESS || !instance) {
        LOGE("❌ [STEP 3] vkCreateInstance failed with error code: %d", (int)res);
        return 2;
    }
    LOGI("✔ [STEP 3] Vulkan Instance created successfully: %p", instance);

    // Lấy function pointers
    LOGI("[STEP 4/7] Resolving Instance level functions...");
    PFN_vkEnumeratePhysicalDevices fnEnumDevices =
        (PFN_vkEnumeratePhysicalDevices)vkGetInstanceProcAddr(instance, "vkEnumeratePhysicalDevices");
    PFN_vkGetPhysicalDeviceProperties fnGetProps =
        (PFN_vkGetPhysicalDeviceProperties)vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceProperties");
    PFN_vkGetPhysicalDeviceQueueFamilyProperties fnGetQueueProps =
        (PFN_vkGetPhysicalDeviceQueueFamilyProperties)vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceQueueFamilyProperties");
    PFN_vkCreateDevice fnCreateDevice =
        (PFN_vkCreateDevice)vkGetInstanceProcAddr(instance, "vkCreateDevice");
    PFN_vkDestroyDevice fnDestroyDevice =
        (PFN_vkDestroyDevice)vkGetInstanceProcAddr(instance, "vkDestroyDevice");
    PFN_vkDestroyInstance fnDestroyInstance =
        (PFN_vkDestroyInstance)vkGetInstanceProcAddr(instance, "vkDestroyInstance");
    PFN_vkDestroySurfaceKHR fnDestroySurface =
        (PFN_vkDestroySurfaceKHR)vkGetInstanceProcAddr(instance, "vkDestroySurfaceKHR");

    if (!fnEnumDevices || !fnGetProps) {
        LOGE("❌ [STEP 4] Failed to resolve essential instance functions");
        return 3;
    }
    LOGI("✔ [STEP 4] Instance functions resolved successfully");

    // STEP 5: Liệt kê Physical GPU Devices
    LOGI("[STEP 5/7] Enumerating Physical GPUs...");
    uint32_t deviceCount = 0;
    fnEnumDevices(instance, &deviceCount, NULL);
    LOGI("🔍 Physical GPU Device Count: %u", deviceCount);
    if (deviceCount == 0) {
        LOGE("❌ No Vulkan-compatible physical devices found!");
        return 3;
    }

    VkPhysicalDevice physDevice = NULL;
    fnEnumDevices(instance, &deviceCount, &physDevice);

    VkPhysicalDeviceProperties devProps;
    fnGetProps(physDevice, &devProps);
    LOGI("=================================================");
    LOGI("📱 GPU Device Name    : %s", devProps.deviceName);
    LOGI("🔧 Vendor ID / Dev ID : 0x%04x / 0x%04x", devProps.vendorID, devProps.deviceID);
    LOGI("⚡ Vulkan API Version : %d.%d.%d",
         devProps.apiVersion >> 22, (devProps.apiVersion >> 12) & 0x3ff, devProps.apiVersion & 0xfff);
    LOGI("🚀 Driver Version     : %u", devProps.driverVersion);
    LOGI("=================================================");

    // STEP 6: Tạo Vulkan Surface từ Android NativeWindow
    LOGI("[STEP 6/7] Creating Android Surface (Metal translation)...");
    void* nativeWindow = ANativeWindow_fromSurface(NULL, NULL);
    VkAndroidSurfaceCreateInfoKHR surfaceInfo;
    surfaceInfo.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    surfaceInfo.pNext = NULL;
    surfaceInfo.flags = 0;
    surfaceInfo.window = nativeWindow;

    VkSurfaceKHR surface = NULL;
    res = vkCreateAndroidSurfaceKHR(instance, &surfaceInfo, NULL, &surface);
    if (res != VK_SUCCESS || !surface) {
        LOGE("❌ vkCreateAndroidSurfaceKHR failed: %d", (int)res);
        return 5;
    }
    LOGI("✔ [STEP 6] VkSurfaceKHR translated & created: %p", surface);

    // STEP 7: Dọn dẹp tài nguyên
    LOGI("[STEP 7/7] Cleaning up Vulkan resources...");
    if (fnDestroySurface) fnDestroySurface(instance, surface, NULL);
    if (fnDestroyInstance) fnDestroyInstance(instance, NULL);

    LOGI("=================================================");
    LOGI("🎉 SUCCESS: VULKAN & MOLTENVK SUBSYSTEM PASSED 100%!");
    LOGI("=================================================");
    return 0;
}
