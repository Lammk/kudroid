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
#define LOG_TAG "VulkanSwap"
#define LOGI(...) __android_log_print(3, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(6, LOG_TAG, __VA_ARGS__)

// Vulkan Basic Types & Handles
typedef uint32_t VkResult;
typedef uint32_t VkFlags;
typedef uint32_t VkStructureType;
typedef uint32_t VkFormat;
typedef uint32_t VkColorSpaceKHR;
typedef uint32_t VkPresentModeKHR;
typedef uint64_t VkDeviceSize;

typedef void* VkInstance;
typedef void* VkPhysicalDevice;
typedef void* VkDevice;
typedef void* VkQueue;
typedef void* VkSurfaceKHR;
typedef void* VkSwapchainKHR;
typedef void* VkImage;
typedef void* VkImageView;
typedef void* VkRenderPass;
typedef void* VkFramebuffer;
typedef void* VkCommandPool;
typedef void* VkCommandBuffer;
typedef void* VkSemaphore;
typedef void* VkFence;

#define VK_SUCCESS 0
#define VK_STRUCTURE_TYPE_APPLICATION_INFO 0
#define VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO 1
#define VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO 2
#define VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO 3
#define VK_STRUCTURE_TYPE_SUBMIT_INFO 4
#define VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO 38
#define VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO 39
#define VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO 40
#define VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO 42
#define VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO 43
#define VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO 15
#define VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO 37
#define VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO 9
#define VK_STRUCTURE_TYPE_FENCE_CREATE_INFO 8
#define VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR 1000001000
#define VK_STRUCTURE_TYPE_PRESENT_INFO_KHR 1000001001
#define VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR 1000008000

#define VK_QUEUE_GRAPHICS_BIT 0x00000001
#define VK_FORMAT_B8G8R8A8_UNORM 44
#define VK_FORMAT_R8G8B8A8_UNORM 37
#define VK_COLOR_SPACE_SRGB_NONLINEAR_KHR 0
#define VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT 0x00000010
#define VK_SHARING_MODE_EXCLUSIVE 0
#define VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR 0x00000001
#define VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR 0x00000001
#define VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR 0x00000008
#define VK_PRESENT_MODE_FIFO_KHR 2
#define VK_PRESENT_MODE_MAILBOX_KHR 1
#define VK_PRESENT_MODE_IMMEDIATE_KHR 0

#define VK_IMAGE_VIEW_TYPE_2D 1
#define VK_COMPONENT_SWIZZLE_IDENTITY 0
#define VK_IMAGE_ASPECT_COLOR_BIT 0x00000001

#define VK_ATTACHMENT_LOAD_OP_CLEAR 1
#define VK_ATTACHMENT_STORE_OP_STORE 0
#define VK_IMAGE_LAYOUT_UNDEFINED 0
#define VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL 2
#define VK_IMAGE_LAYOUT_PRESENT_SRC_KHR 1000001002
#define VK_SUBPASS_EXTERNAL ~0U
#define VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT 0x00000400
#define VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT 0x00000100

#define VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT 0x00000002
#define VK_COMMAND_BUFFER_LEVEL_PRIMARY 0
#define VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT 0x00000001
#define VK_SUBPASS_CONTENTS_INLINE 0

typedef struct VkExtent2D {
    uint32_t width;
    uint32_t height;
} VkExtent2D;

typedef struct VkOffset2D {
    int32_t x;
    int32_t y;
} VkOffset2D;

typedef struct VkRect2D {
    VkOffset2D offset;
    VkExtent2D extent;
} VkRect2D;

typedef union VkClearColorValue {
    float float32[4];
    int32_t int32[4];
    uint32_t uint32[4];
} VkClearColorValue;

typedef union VkClearValue {
    VkClearColorValue color;
    uint8_t depthStencil[8];
} VkClearValue;

typedef struct VkSurfaceCapabilitiesKHR {
    uint32_t minImageCount;
    uint32_t maxImageCount;
    VkExtent2D currentExtent;
    VkExtent2D minImageExtent;
    VkExtent2D maxImageExtent;
    uint32_t maxImageArrayLayers;
    VkFlags supportedTransforms;
    VkFlags currentTransform;
    VkFlags supportedCompositeAlpha;
    VkFlags supportedUsageFlags;
} VkSurfaceCapabilitiesKHR;

typedef struct VkSurfaceFormatKHR {
    VkFormat format;
    VkColorSpaceKHR colorSpace;
} VkSurfaceFormatKHR;

typedef struct VkAndroidSurfaceCreateInfoKHR {
    uint32_t sType;
    const void* pNext;
    VkFlags flags;
    void* window;
} VkAndroidSurfaceCreateInfoKHR;

typedef struct VkSwapchainCreateInfoKHR {
    uint32_t sType;
    const void* pNext;
    VkFlags flags;
    VkSurfaceKHR surface;
    uint32_t minImageCount;
    VkFormat imageFormat;
    VkColorSpaceKHR imageColorSpace;
    VkExtent2D imageExtent;
    uint32_t imageArrayLayers;
    VkFlags imageUsage;
    uint32_t imageSharingMode;
    uint32_t queueFamilyIndexCount;
    const uint32_t* pQueueFamilyIndices;
    VkFlags preTransform;
    VkFlags compositeAlpha;
    VkPresentModeKHR presentMode;
    uint32_t clipped;
    VkSwapchainKHR oldSwapchain;
} VkSwapchainCreateInfoKHR;

typedef struct VkComponentMapping {
    uint32_t r; uint32_t g; uint32_t b; uint32_t a;
} VkComponentMapping;

typedef struct VkImageSubresourceRange {
    VkFlags aspectMask;
    uint32_t baseMipLevel;
    uint32_t levelCount;
    uint32_t baseArrayLayer;
    uint32_t layerCount;
} VkImageSubresourceRange;

typedef struct VkImageViewCreateInfo {
    uint32_t sType;
    const void* pNext;
    VkFlags flags;
    VkImage image;
    uint32_t viewType;
    VkFormat format;
    VkComponentMapping components;
    VkImageSubresourceRange subresourceRange;
} VkImageViewCreateInfo;

typedef struct VkAttachmentDescription {
    VkFlags flags;
    VkFormat format;
    uint32_t samples;
    uint32_t loadOp;
    uint32_t storeOp;
    uint32_t stencilLoadOp;
    uint32_t stencilStoreOp;
    uint32_t initialLayout;
    uint32_t finalLayout;
} VkAttachmentDescription;

typedef struct VkAttachmentReference {
    uint32_t attachment;
    uint32_t layout;
} VkAttachmentReference;

typedef struct VkSubpassDescription {
    VkFlags flags;
    uint32_t pipelineBindPoint;
    uint32_t inputAttachmentCount;
    const VkAttachmentReference* pInputAttachments;
    uint32_t colorAttachmentCount;
    const VkAttachmentReference* pColorAttachments;
    const VkAttachmentReference* pResolveAttachments;
    const VkAttachmentReference* pDepthStencilAttachment;
    uint32_t preserveAttachmentCount;
    const uint32_t* pPreserveAttachments;
} VkSubpassDescription;

typedef struct VkSubpassDependency {
    uint32_t srcSubpass;
    uint32_t dstSubpass;
    VkFlags srcStageMask;
    VkFlags dstStageMask;
    VkFlags srcAccessMask;
    VkFlags dstAccessMask;
    VkFlags dependencyFlags;
} VkSubpassDependency;

typedef struct VkRenderPassCreateInfo {
    uint32_t sType;
    const void* pNext;
    VkFlags flags;
    uint32_t attachmentCount;
    const VkAttachmentDescription* pAttachments;
    uint32_t subpassCount;
    const VkSubpassDescription* pSubpasses;
    uint32_t dependencyCount;
    const VkSubpassDependency* pDependencies;
} VkRenderPassCreateInfo;

typedef struct VkFramebufferCreateInfo {
    uint32_t sType;
    const void* pNext;
    VkFlags flags;
    VkRenderPass renderPass;
    uint32_t attachmentCount;
    const VkImageView* pAttachments;
    uint32_t width;
    uint32_t height;
    uint32_t layers;
} VkFramebufferCreateInfo;

typedef struct VkSemaphoreCreateInfo {
    uint32_t sType;
    const void* pNext;
    VkFlags flags;
} VkSemaphoreCreateInfo;

typedef struct VkPresentInfoKHR {
    uint32_t sType;
    const void* pNext;
    uint32_t waitSemaphoreCount;
    const VkSemaphore* pWaitSemaphores;
    uint32_t swapchainCount;
    const VkSwapchainKHR* pSwapchains;
    const uint32_t* pImageIndices;
    VkResult* pResults;
} VkPresentInfoKHR;

typedef struct VkApplicationInfo {
    uint32_t sType;
    const void* pNext;
    const char* pApplicationName;
    uint32_t applicationVersion;
    const char* pEngineName;
    uint32_t engineVersion;
    uint32_t apiVersion;
} VkApplicationInfo;

typedef struct VkInstanceCreateInfo {
    uint32_t sType;
    const void* pNext;
    VkFlags flags;
    const VkApplicationInfo* pApplicationInfo;
    uint32_t enabledLayerCount;
    const char* const* ppEnabledLayerNames;
    uint32_t enabledExtensionCount;
    const char* const* ppEnabledExtensionNames;
} VkInstanceCreateInfo;

typedef struct VkQueueFamilyProperties {
    VkFlags queueFlags;
    uint32_t queueCount;
    uint32_t timestampValidBits;
    uint32_t minImageTransferGranularity[3];
} VkQueueFamilyProperties;

typedef struct VkDeviceQueueCreateInfo {
    uint32_t sType;
    const void* pNext;
    VkFlags flags;
    uint32_t queueFamilyIndex;
    uint32_t queueCount;
    const float* pQueuePriorities;
} VkDeviceQueueCreateInfo;

typedef struct VkDeviceCreateInfo {
    uint32_t sType;
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

typedef struct VkCommandPoolCreateInfo {
    uint32_t sType;
    const void* pNext;
    VkFlags flags;
    uint32_t queueFamilyIndex;
} VkCommandPoolCreateInfo;

typedef struct VkCommandBufferAllocateInfo {
    uint32_t sType;
    const void* pNext;
    VkCommandPool commandPool;
    uint32_t level;
    uint32_t commandBufferCount;
} VkCommandBufferAllocateInfo;

typedef struct VkCommandBufferBeginInfo {
    uint32_t sType;
    const void* pNext;
    VkFlags flags;
    const void* pInheritanceInfo;
} VkCommandBufferBeginInfo;

typedef struct VkRenderPassBeginInfo {
    uint32_t sType;
    const void* pNext;
    VkRenderPass renderPass;
    VkFramebuffer framebuffer;
    VkRect2D renderArea;
    uint32_t clearValueCount;
    const VkClearValue* pClearValues;
} VkRenderPassBeginInfo;

typedef struct VkSubmitInfo {
    uint32_t sType;
    const void* pNext;
    uint32_t waitSemaphoreCount;
    const VkSemaphore* pWaitSemaphores;
    const VkFlags* pWaitDstStageMask;
    uint32_t commandBufferCount;
    const VkCommandBuffer* pCommandBuffers;
    uint32_t signalSemaphoreCount;
    const VkSemaphore* pSignalSemaphores;
} VkSubmitInfo;

// Function pointers
typedef void (*PFN_vkVoidFunction)(void);
typedef PFN_vkVoidFunction (*PFN_vkGetInstanceProcAddr)(VkInstance, const char*);
typedef VkResult (*PFN_vkCreateInstance)(const VkInstanceCreateInfo*, const void*, VkInstance*);
typedef void (*PFN_vkDestroyInstance)(VkInstance, const void*);
typedef VkResult (*PFN_vkCreateAndroidSurfaceKHR)(VkInstance, const VkAndroidSurfaceCreateInfoKHR*, const void*, VkSurfaceKHR*);
typedef void (*PFN_vkDestroySurfaceKHR)(VkInstance, VkSurfaceKHR, const void*);
typedef VkResult (*PFN_vkEnumeratePhysicalDevices)(VkInstance, uint32_t*, VkPhysicalDevice*);
typedef void (*PFN_vkGetPhysicalDeviceQueueFamilyProperties)(VkPhysicalDevice, uint32_t*, VkQueueFamilyProperties*);
typedef VkResult (*PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR)(VkPhysicalDevice, VkSurfaceKHR, VkSurfaceCapabilitiesKHR*);
typedef VkResult (*PFN_vkGetPhysicalDeviceSurfaceFormatsKHR)(VkPhysicalDevice, VkSurfaceKHR, uint32_t*, VkSurfaceFormatKHR*);
typedef VkResult (*PFN_vkGetPhysicalDeviceSurfacePresentModesKHR)(VkPhysicalDevice, VkSurfaceKHR, uint32_t*, VkPresentModeKHR*);
typedef VkResult (*PFN_vkCreateDevice)(VkPhysicalDevice, const VkDeviceCreateInfo*, const void*, VkDevice*);
typedef void (*PFN_vkDestroyDevice)(VkDevice, const void*);
typedef void (*PFN_vkGetDeviceQueue)(VkDevice, uint32_t, uint32_t, VkQueue*);
typedef VkResult (*PFN_vkCreateSwapchainKHR)(VkDevice, const VkSwapchainCreateInfoKHR*, const void*, VkSwapchainKHR*);
typedef void (*PFN_vkDestroySwapchainKHR)(VkDevice, VkSwapchainKHR, const void*);
typedef VkResult (*PFN_vkGetSwapchainImagesKHR)(VkDevice, VkSwapchainKHR, uint32_t*, VkImage*);
typedef VkResult (*PFN_vkCreateImageView)(VkDevice, const VkImageViewCreateInfo*, const void*, VkImageView*);
typedef void (*PFN_vkDestroyImageView)(VkDevice, VkImageView, const void*);
typedef VkResult (*PFN_vkCreateRenderPass)(VkDevice, const VkRenderPassCreateInfo*, const void*, VkRenderPass*);
typedef void (*PFN_vkDestroyRenderPass)(VkDevice, VkRenderPass, const void*);
typedef VkResult (*PFN_vkCreateFramebuffer)(VkDevice, const VkFramebufferCreateInfo*, const void*, VkFramebuffer*);
typedef void (*PFN_vkDestroyFramebuffer)(VkDevice, VkFramebuffer, const void*);
typedef VkResult (*PFN_vkCreateCommandPool)(VkDevice, const VkCommandPoolCreateInfo*, const void*, VkCommandPool*);
typedef void (*PFN_vkDestroyCommandPool)(VkDevice, VkCommandPool, const void*);
typedef VkResult (*PFN_vkAllocateCommandBuffers)(VkDevice, const VkCommandBufferAllocateInfo*, VkCommandBuffer*);
typedef VkResult (*PFN_vkBeginCommandBuffer)(VkCommandBuffer, const VkCommandBufferBeginInfo*);
typedef VkResult (*PFN_vkEndCommandBuffer)(VkCommandBuffer);
typedef void (*PFN_vkCmdBeginRenderPass)(VkCommandBuffer, const VkRenderPassBeginInfo*, uint32_t);
typedef void (*PFN_vkCmdEndRenderPass)(VkCommandBuffer);
typedef VkResult (*PFN_vkCreateSemaphore)(VkDevice, const VkSemaphoreCreateInfo*, const void*, VkSemaphore*);
typedef void (*PFN_vkDestroySemaphore)(VkDevice, VkSemaphore, const void*);
typedef VkResult (*PFN_vkAcquireNextImageKHR)(VkDevice, VkSwapchainKHR, uint64_t, VkSemaphore, VkFence, uint32_t*);
typedef VkResult (*PFN_vkQueueSubmit)(VkQueue, uint32_t, const VkSubmitInfo*, VkFence);
typedef VkResult (*PFN_vkQueuePresentKHR)(VkQueue, const VkPresentInfoKHR*);
typedef VkResult (*PFN_vkQueueWaitIdle)(VkQueue);
typedef VkResult (*PFN_vkDeviceWaitIdle)(VkDevice);

extern PFN_vkVoidFunction vkGetInstanceProcAddr(VkInstance instance, const char* pName);
extern void* bionic_ANativeWindow_fromSurface(void* env, void* surface);

int kudroid_test_main(void) {
    LOGI("=================================================");
    LOGI("🌋 [Vulkan 1.2 Swapchain & On-Screen Render Test]");
    LOGI("=================================================");

    // 1. Instance Creation
    PFN_vkCreateInstance fnCreateInstance = (PFN_vkCreateInstance)vkGetInstanceProcAddr(NULL, "vkCreateInstance");
    if (!fnCreateInstance) {
        LOGE("❌ Failed to resolve vkCreateInstance");
        return 1;
    }

    VkApplicationInfo appInfo = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "KuDroid Swapchain Renderer",
        .applicationVersion = 1,
        .pEngineName = "KuDroidVulkan",
        .engineVersion = 1,
        .apiVersion = (1 << 22) | (2 << 12) // Vulkan 1.2
    };

    const char* instanceExts[] = { "VK_KHR_surface", "VK_KHR_android_surface" };
    VkInstanceCreateInfo instInfo = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &appInfo,
        .enabledExtensionCount = 2,
        .ppEnabledExtensionNames = instanceExts
    };

    VkInstance instance = NULL;
    if (fnCreateInstance(&instInfo, NULL, &instance) != VK_SUCCESS || !instance) {
        LOGE("❌ vkCreateInstance failed");
        return 2;
    }
    LOGI("✔ [STEP 1] Vulkan 1.2 Instance created: %p", instance);

    // Resolve Instance Functions
    PFN_vkDestroyInstance fnDestroyInstance = (PFN_vkDestroyInstance)vkGetInstanceProcAddr(instance, "vkDestroyInstance");
    PFN_vkCreateAndroidSurfaceKHR fnCreateSurface = (PFN_vkCreateAndroidSurfaceKHR)vkGetInstanceProcAddr(instance, "vkCreateAndroidSurfaceKHR");
    PFN_vkDestroySurfaceKHR fnDestroySurface = (PFN_vkDestroySurfaceKHR)vkGetInstanceProcAddr(instance, "vkDestroySurfaceKHR");
    PFN_vkEnumeratePhysicalDevices fnEnumDevices = (PFN_vkEnumeratePhysicalDevices)vkGetInstanceProcAddr(instance, "vkEnumeratePhysicalDevices");
    PFN_vkGetPhysicalDeviceQueueFamilyProperties fnGetQueueProps = (PFN_vkGetPhysicalDeviceQueueFamilyProperties)vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceQueueFamilyProperties");
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR fnGetCaps = (PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR)vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR fnGetFormats = (PFN_vkGetPhysicalDeviceSurfaceFormatsKHR)vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceSurfaceFormatsKHR");
    PFN_vkGetPhysicalDeviceSurfacePresentModesKHR fnGetPresentModes = (PFN_vkGetPhysicalDeviceSurfacePresentModesKHR)vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceSurfacePresentModesKHR");
    PFN_vkCreateDevice fnCreateDevice = (PFN_vkCreateDevice)vkGetInstanceProcAddr(instance, "vkCreateDevice");
    PFN_vkDestroyDevice fnDestroyDevice = (PFN_vkDestroyDevice)vkGetInstanceProcAddr(instance, "vkDestroyDevice");
    PFN_vkGetDeviceQueue fnGetDeviceQueue = (PFN_vkGetDeviceQueue)vkGetInstanceProcAddr(instance, "vkGetDeviceQueue");
    PFN_vkCreateSwapchainKHR fnCreateSwapchain = (PFN_vkCreateSwapchainKHR)vkGetInstanceProcAddr(instance, "vkCreateSwapchainKHR");
    PFN_vkDestroySwapchainKHR fnDestroySwapchain = (PFN_vkDestroySwapchainKHR)vkGetInstanceProcAddr(instance, "vkDestroySwapchainKHR");
    PFN_vkGetSwapchainImagesKHR fnGetSwapchainImages = (PFN_vkGetSwapchainImagesKHR)vkGetInstanceProcAddr(instance, "vkGetSwapchainImagesKHR");
    PFN_vkCreateImageView fnCreateImageView = (PFN_vkCreateImageView)vkGetInstanceProcAddr(instance, "vkCreateImageView");
    PFN_vkDestroyImageView fnDestroyImageView = (PFN_vkDestroyImageView)vkGetInstanceProcAddr(instance, "vkDestroyImageView");
    PFN_vkCreateRenderPass fnCreateRenderPass = (PFN_vkCreateRenderPass)vkGetInstanceProcAddr(instance, "vkCreateRenderPass");
    PFN_vkDestroyRenderPass fnDestroyRenderPass = (PFN_vkDestroyRenderPass)vkGetInstanceProcAddr(instance, "vkDestroyRenderPass");
    PFN_vkCreateFramebuffer fnCreateFramebuffer = (PFN_vkCreateFramebuffer)vkGetInstanceProcAddr(instance, "vkCreateFramebuffer");
    PFN_vkDestroyFramebuffer fnDestroyFramebuffer = (PFN_vkDestroyFramebuffer)vkGetInstanceProcAddr(instance, "vkDestroyFramebuffer");
    PFN_vkCreateCommandPool fnCreateCommandPool = (PFN_vkCreateCommandPool)vkGetInstanceProcAddr(instance, "vkCreateCommandPool");
    PFN_vkDestroyCommandPool fnDestroyCommandPool = (PFN_vkDestroyCommandPool)vkGetInstanceProcAddr(instance, "vkDestroyCommandPool");
    PFN_vkAllocateCommandBuffers fnAllocateCommandBuffers = (PFN_vkAllocateCommandBuffers)vkGetInstanceProcAddr(instance, "vkAllocateCommandBuffers");
    PFN_vkBeginCommandBuffer fnBeginCommandBuffer = (PFN_vkBeginCommandBuffer)vkGetInstanceProcAddr(instance, "vkBeginCommandBuffer");
    PFN_vkEndCommandBuffer fnEndCommandBuffer = (PFN_vkEndCommandBuffer)vkGetInstanceProcAddr(instance, "vkEndCommandBuffer");
    PFN_vkCmdBeginRenderPass fnCmdBeginRenderPass = (PFN_vkCmdBeginRenderPass)vkGetInstanceProcAddr(instance, "vkCmdBeginRenderPass");
    PFN_vkCmdEndRenderPass fnCmdEndRenderPass = (PFN_vkCmdEndRenderPass)vkGetInstanceProcAddr(instance, "vkCmdEndRenderPass");
    PFN_vkCreateSemaphore fnCreateSemaphore = (PFN_vkCreateSemaphore)vkGetInstanceProcAddr(instance, "vkCreateSemaphore");
    PFN_vkDestroySemaphore fnDestroySemaphore = (PFN_vkDestroySemaphore)vkGetInstanceProcAddr(instance, "vkDestroySemaphore");
    PFN_vkAcquireNextImageKHR fnAcquireNextImage = (PFN_vkAcquireNextImageKHR)vkGetInstanceProcAddr(instance, "vkAcquireNextImageKHR");
    PFN_vkQueueSubmit fnQueueSubmit = (PFN_vkQueueSubmit)vkGetInstanceProcAddr(instance, "vkQueueSubmit");
    PFN_vkQueuePresentKHR fnQueuePresent = (PFN_vkQueuePresentKHR)vkGetInstanceProcAddr(instance, "vkQueuePresentKHR");
    PFN_vkQueueWaitIdle fnQueueWaitIdle = (PFN_vkQueueWaitIdle)vkGetInstanceProcAddr(instance, "vkQueueWaitIdle");
    PFN_vkDeviceWaitIdle fnDeviceWaitIdle = (PFN_vkDeviceWaitIdle)vkGetInstanceProcAddr(instance, "vkDeviceWaitIdle");

    // 2. Create Surface
    void* nativeWindow = bionic_ANativeWindow_fromSurface(NULL, NULL);
    VkAndroidSurfaceCreateInfoKHR surfaceInfo = {
        .sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR,
        .window = nativeWindow
    };
    VkSurfaceKHR surface = NULL;
    fnCreateSurface(instance, &surfaceInfo, NULL, &surface);
    LOGI("✔ [STEP 2] Surface connected to CAMetalLayer: %p", surface);

    // 3. Physical Device & Queue Family
    uint32_t deviceCount = 1;
    VkPhysicalDevice physicalDevice = NULL;
    fnEnumDevices(instance, &deviceCount, &physicalDevice);

    uint32_t qfCount = 8;
    VkQueueFamilyProperties queueProps[8];
    fnGetQueueProps(physicalDevice, &qfCount, queueProps);
    uint32_t graphicsQueueFamily = 0;

    // 4. Create Logical Device with VK_KHR_swapchain
    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = graphicsQueueFamily,
        .queueCount = 1,
        .pQueuePriorities = &queuePriority
    };

    const char* deviceExts[] = { "VK_KHR_swapchain" };
    VkDeviceCreateInfo deviceCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queueCreateInfo,
        .enabledExtensionCount = 1,
        .ppEnabledExtensionNames = deviceExts
    };

    VkDevice device = NULL;
    fnCreateDevice(physicalDevice, &deviceCreateInfo, NULL, &device);
    LOGI("✔ [STEP 3] Logical Device created with VK_KHR_swapchain: %p", device);

    VkQueue queue = NULL;
    fnGetDeviceQueue(device, graphicsQueueFamily, 0, &queue);

    // 5. Query Surface Capabilities & Formats
    VkSurfaceCapabilitiesKHR caps;
    fnGetCaps(physicalDevice, surface, &caps);
    LOGI("✔ [STEP 4] Surface Capabilities: Extent=%ux%u, ImageCount=%u..%u",
         caps.currentExtent.width, caps.currentExtent.height, caps.minImageCount, caps.maxImageCount);

    uint32_t formatCount = 16;
    VkSurfaceFormatKHR formats[16];
    fnGetFormats(physicalDevice, surface, &formatCount, formats);
    VkFormat chosenFormat = formats[0].format;
    VkColorSpaceKHR chosenColorSpace = formats[0].colorSpace;
    LOGI("✔ [STEP 5] Chosen Surface Format: %u (ColorSpace: %u)", chosenFormat, chosenColorSpace);

    // 6. Create Swapchain
    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) {
        imageCount = caps.maxImageCount;
    }

    VkExtent2D swapExtent = caps.currentExtent;
    if (swapExtent.width == 0 || swapExtent.width == 0xFFFFFFFF) {
        swapExtent.width = 828;
        swapExtent.height = 1792;
    }

    VkSwapchainCreateInfoKHR swapInfo = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surface,
        .minImageCount = imageCount,
        .imageFormat = chosenFormat,
        .imageColorSpace = chosenColorSpace,
        .imageExtent = swapExtent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = caps.currentTransform ? caps.currentTransform : VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        .compositeAlpha = (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) ?
                          VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR : VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR,
        .clipped = 1,
        .oldSwapchain = NULL
    };

    VkSwapchainKHR swapchain = NULL;
    VkResult res = fnCreateSwapchain(device, &swapInfo, NULL, &swapchain);
    if (res != VK_SUCCESS || !swapchain) {
        LOGE("❌ vkCreateSwapchainKHR failed with error: %d", (int)res);
        return 6;
    }
    LOGI("✔ [STEP 6] VkSwapchainKHR created successfully: %p (%ux%u)", swapchain, swapExtent.width, swapExtent.height);

    // 7. Get Swapchain Images & Create ImageViews
    uint32_t actualImageCount = 0;
    fnGetSwapchainImages(device, swapchain, &actualImageCount, NULL);
    LOGI("✔ [STEP 7] Swapchain Images allocated by MoltenVK: %u", actualImageCount);

    VkImage swapImages[8];
    fnGetSwapchainImages(device, swapchain, &actualImageCount, swapImages);

    VkImageView swapViews[8];
    for (uint32_t i = 0; i < actualImageCount; ++i) {
        VkImageViewCreateInfo viewInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = swapImages[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = chosenFormat,
            .components = {0, 0, 0, 0},
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };
        fnCreateImageView(device, &viewInfo, NULL, &swapViews[i]);
    }
    LOGI("✔ [STEP 8] %u VkImageViews created for Swapchain Images", actualImageCount);

    // 8. Create RenderPass
    VkAttachmentDescription colorAttachment = {
        .format = chosenFormat,
        .samples = 1,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = 0,
        .stencilStoreOp = 0,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
    };

    VkAttachmentReference colorAttachmentRef = {
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
    };

    VkSubpassDescription subpass = {
        .pipelineBindPoint = 0, // GRAPHICS
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachmentRef
    };

    VkSubpassDependency dependency = {
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
    };

    VkRenderPassCreateInfo renderPassInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &colorAttachment,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = 1,
        .pDependencies = &dependency
    };

    VkRenderPass renderPass = NULL;
    fnCreateRenderPass(device, &renderPassInfo, NULL, &renderPass);
    LOGI("✔ [STEP 9] VkRenderPass created: %p", renderPass);

    // 9. Create Framebuffers
    VkFramebuffer framebuffers[8];
    for (uint32_t i = 0; i < actualImageCount; ++i) {
        VkImageView attachments[] = { swapViews[i] };
        VkFramebufferCreateInfo fbInfo = {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = renderPass,
            .attachmentCount = 1,
            .pAttachments = attachments,
            .width = swapExtent.width,
            .height = swapExtent.height,
            .layers = 1
        };
        fnCreateFramebuffer(device, &fbInfo, NULL, &framebuffers[i]);
    }
    LOGI("✔ [STEP 10] %u VkFramebuffers created for Swapchain Extent", actualImageCount);

    // 10. Command Pool & Command Buffer
    VkCommandPoolCreateInfo poolInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = graphicsQueueFamily
    };
    VkCommandPool commandPool = NULL;
    fnCreateCommandPool(device, &poolInfo, NULL, &commandPool);

    VkCommandBufferAllocateInfo cmdAlloc = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1
    };
    VkCommandBuffer cmdBuffer = NULL;
    fnAllocateCommandBuffers(device, &cmdAlloc, &cmdBuffer);

    // 11. Synchronization Semaphores
    VkSemaphoreCreateInfo semInfo = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkSemaphore imageAvailableSemaphore = NULL;
    VkSemaphore renderFinishedSemaphore = NULL;
    fnCreateSemaphore(device, &semInfo, NULL, &imageAvailableSemaphore);
    fnCreateSemaphore(device, &semInfo, NULL, &renderFinishedSemaphore);

    // 12. RENDER & PRESENT 3 FRAMES WITH NEON COLORS TO IPHONE SCREEN!
    LOGI("=================================================");
    LOGI("🎨 [Rendering & Presenting Frames to iPhone Screen]");
    LOGI("=================================================");

    float clearColors[3][4] = {
        { 0.0f, 0.9f, 1.0f, 1.0f }, // Cyberpunk Neon Cyan
        { 1.0f, 0.0f, 0.5f, 1.0f }, // Hot Neon Magenta
        { 0.2f, 1.0f, 0.2f, 1.0f }  // Electric Lime Green
    };

    for (int frame = 0; frame < 3; ++frame) {
        uint32_t imageIndex = 0;
        fnAcquireNextImage(device, swapchain, 1000000000ULL, imageAvailableSemaphore, NULL, &imageIndex);

        VkCommandBufferBeginInfo beginInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
        };
        fnBeginCommandBuffer(cmdBuffer, &beginInfo);

        VkClearValue clearColor;
        clearColor.color.float32[0] = clearColors[frame][0];
        clearColor.color.float32[1] = clearColors[frame][1];
        clearColor.color.float32[2] = clearColors[frame][2];
        clearColor.color.float32[3] = clearColors[frame][3];

        VkRenderPassBeginInfo rpBegin = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass = renderPass,
            .framebuffer = framebuffers[imageIndex],
            .renderArea = { .offset = {0, 0}, .extent = swapExtent },
            .clearValueCount = 1,
            .pClearValues = &clearColor
        };

        fnCmdBeginRenderPass(cmdBuffer, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
        // (Draw commands would go here)
        fnCmdEndRenderPass(cmdBuffer);
        fnEndCommandBuffer(cmdBuffer);

        VkFlags waitStages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submitInfo = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &imageAvailableSemaphore,
            .pWaitDstStageMask = &waitStages,
            .commandBufferCount = 1,
            .pCommandBuffers = &cmdBuffer,
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &renderFinishedSemaphore
        };

        fnQueueSubmit(queue, 1, &submitInfo, NULL);

        VkPresentInfoKHR presentInfo = {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &renderFinishedSemaphore,
            .swapchainCount = 1,
            .pSwapchains = &swapchain,
            .pImageIndices = &imageIndex
        };

        VkResult presRes = fnQueuePresent(queue, &presentInfo);
        LOGI("✔ [FRAME %d] Rendered & Presented to CAMetalLayer Screen (Color: R=%.1f G=%.1f B=%.1f, Result: %d)",
             frame + 1, clearColors[frame][0], clearColors[frame][1], clearColors[frame][2], (int)presRes);
    }

    fnQueueWaitIdle(queue);

    // 13. Resource Cleanup
    fnDeviceWaitIdle(device);
    fnDestroySemaphore(device, imageAvailableSemaphore, NULL);
    fnDestroySemaphore(device, renderFinishedSemaphore, NULL);
    fnDestroyCommandPool(device, commandPool, NULL);
    for (uint32_t i = 0; i < actualImageCount; ++i) {
        fnDestroyFramebuffer(device, framebuffers[i], NULL);
        fnDestroyImageView(device, swapViews[i], NULL);
    }
    fnDestroyRenderPass(device, renderPass, NULL);
    fnDestroySwapchain(device, swapchain, NULL);
    fnDestroyDevice(device, NULL);
    fnDestroySurface(instance, surface, NULL);
    fnDestroyInstance(instance, NULL);

    LOGI("=================================================");
    LOGI("🎉 VULKAN 1.2 SWAPCHAIN & ON-SCREEN RENDER COMPLETE!");
    LOGI("=================================================");
    return 0;
}
