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
#define LOG_TAG "VulkanAudit"
#define LOGI(...) __android_log_print(3, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(6, LOG_TAG, __VA_ARGS__)

extern int setenv(const char *name, const char *value, int overwrite);

#include "triangle_vert_spv.h"
#include "triangle_frag_spv.h"

// Vulkan Basic Types & Handles
typedef uint32_t VkResult;
typedef uint32_t VkFlags;
typedef uint32_t VkStructureType;
typedef uint32_t VkFormat;
typedef uint32_t VkColorSpaceKHR;
typedef uint32_t VkPresentModeKHR;
typedef uint64_t VkDeviceSize;
typedef uint32_t VkBool32;

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
typedef void* VkShaderModule;
typedef void* VkPipelineLayout;
typedef void* VkPipeline;

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
#define VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR 1000001000
#define VK_STRUCTURE_TYPE_PRESENT_INFO_KHR 1000001001
#define VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR 1000008000
#define VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO 16
#define VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO 18
#define VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO 19
#define VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO 20
#define VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO 22
#define VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO 23
#define VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO 24
#define VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO 26
#define VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO 28
#define VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO 29
#define VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO 9

#define VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT 0x00000010
#define VK_SHARING_MODE_EXCLUSIVE 0
#define VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR 0x00000001
#define VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR 0x00000008
#define VK_PRESENT_MODE_FIFO_KHR 2

#define VK_IMAGE_VIEW_TYPE_2D 1
#define VK_IMAGE_ASPECT_COLOR_BIT 0x00000001
#define VK_ATTACHMENT_LOAD_OP_CLEAR 1
#define VK_ATTACHMENT_STORE_OP_STORE 0
#define VK_IMAGE_LAYOUT_UNDEFINED 0
#define VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL 2
#define VK_IMAGE_LAYOUT_PRESENT_SRC_KHR 1000001002

#define VK_QUEUE_GRAPHICS_BIT 0x00000001
#define VK_QUEUE_COMPUTE_BIT 0x00000002
#define VK_QUEUE_TRANSFER_BIT 0x00000004

#define VK_SHADER_STAGE_VERTEX_BIT 0x00000001
#define VK_SHADER_STAGE_FRAGMENT_BIT 0x00000002
#define VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST 3
#define VK_POLYGON_MODE_FILL 0
#define VK_CULL_MODE_NONE 0
#define VK_FRONT_FACE_COUNTER_CLOCKWISE 0
#define VK_SAMPLE_COUNT_1_BIT 0x00000001
#define VK_COLOR_COMPONENT_R_BIT 1
#define VK_COLOR_COMPONENT_G_BIT 2
#define VK_COLOR_COMPONENT_B_BIT 4
#define VK_COLOR_COMPONENT_A_BIT 8
#define VK_PIPELINE_BIND_POINT_GRAPHICS 0

extern void* ANativeWindow_fromSurface(void* env, void* surface);
extern int ANativeWindow_getWidth(void* window);
extern int ANativeWindow_getHeight(void* window);
extern int usleep(unsigned int usec);

typedef struct VkExtent2D { uint32_t width; uint32_t height; } VkExtent2D;
typedef struct VkOffset2D { int32_t x; int32_t y; } VkOffset2D;
typedef struct VkRect2D { VkOffset2D offset; VkExtent2D extent; } VkRect2D;
typedef struct VkViewport { float x; float y; float width; float height; float minDepth; float maxDepth; } VkViewport;
typedef struct VkExtent3D { uint32_t width; uint32_t height; uint32_t depth; } VkExtent3D;

typedef struct VkQueueFamilyProperties {
    VkFlags queueFlags;
    uint32_t queueCount;
    uint32_t timestampValidBits;
    VkExtent3D minImageTransferGranularity;
} VkQueueFamilyProperties;

typedef struct VkSurfaceCapabilitiesKHR {
    uint32_t minImageCount; uint32_t maxImageCount;
    VkExtent2D currentExtent; VkExtent2D minImageExtent; VkExtent2D maxImageExtent;
    uint32_t maxImageArrayLayers; VkFlags supportedTransforms; VkFlags currentTransform;
    VkFlags supportedCompositeAlpha; VkFlags supportedUsageFlags;
} VkSurfaceCapabilitiesKHR;

typedef struct VkSurfaceFormatKHR { VkFormat format; VkColorSpaceKHR colorSpace; } VkSurfaceFormatKHR;

typedef struct VkApplicationInfo {
    uint32_t sType; const void* pNext; const char* pApplicationName;
    uint32_t applicationVersion; const char* pEngineName;
    uint32_t engineVersion; uint32_t apiVersion;
} VkApplicationInfo;

typedef struct VkInstanceCreateInfo {
    uint32_t sType; const void* pNext; VkFlags flags;
    const VkApplicationInfo* pApplicationInfo;
    uint32_t enabledLayerCount; const char* const* ppEnabledLayerNames;
    uint32_t enabledExtensionCount; const char* const* ppEnabledExtensionNames;
} VkInstanceCreateInfo;

typedef struct VkAndroidSurfaceCreateInfoKHR {
    uint32_t sType; const void* pNext; VkFlags flags; void* window;
} VkAndroidSurfaceCreateInfoKHR;

typedef struct VkDeviceQueueCreateInfo {
    uint32_t sType; const void* pNext; VkFlags flags;
    uint32_t queueFamilyIndex; uint32_t queueCount; const float* pQueuePriorities;
} VkDeviceQueueCreateInfo;

typedef struct VkDeviceCreateInfo {
    uint32_t sType; const void* pNext; VkFlags flags;
    uint32_t queueCreateInfoCount; const VkDeviceQueueCreateInfo* pQueueCreateInfos;
    uint32_t enabledLayerCount; const char* const* ppEnabledLayerNames;
    uint32_t enabledExtensionCount; const char* const* ppEnabledExtensionNames;
    const void* pEnabledFeatures;
} VkDeviceCreateInfo;

typedef struct VkSwapchainCreateInfoKHR {
    uint32_t sType; const void* pNext; VkFlags flags;
    VkSurfaceKHR surface; uint32_t minImageCount; VkFormat imageFormat;
    VkColorSpaceKHR imageColorSpace; VkExtent2D imageExtent;
    uint32_t imageArrayLayers; VkFlags imageUsage; uint32_t imageSharingMode;
    uint32_t queueFamilyIndexCount; const uint32_t* pQueueFamilyIndices;
    VkFlags preTransform; VkFlags compositeAlpha; VkPresentModeKHR presentMode;
    uint32_t clipped; VkSwapchainKHR oldSwapchain;
} VkSwapchainCreateInfoKHR;

typedef struct VkImageViewCreateInfo {
    uint32_t sType; const void* pNext; VkFlags flags;
    VkImage image; uint32_t viewType; VkFormat format;
    uint32_t components[4]; uint32_t subresourceRange[5];
} VkImageViewCreateInfo;

typedef struct VkAttachmentDescription {
    VkFlags flags; VkFormat format; uint32_t samples;
    uint32_t loadOp; uint32_t storeOp; uint32_t stencilLoadOp;
    uint32_t stencilStoreOp; uint32_t initialLayout; uint32_t finalLayout;
} VkAttachmentDescription;

typedef struct VkAttachmentReference { uint32_t attachment; uint32_t layout; } VkAttachmentReference;

typedef struct VkSubpassDescription {
    VkFlags flags; uint32_t pipelineBindPoint; uint32_t inputAttachmentCount;
    const VkAttachmentReference* pInputAttachments; uint32_t colorAttachmentCount;
    const VkAttachmentReference* pColorAttachments; const void* pResolveAttachments;
    const void* pDepthStencilAttachment; uint32_t preserveAttachmentCount; const void* pPreserveAttachments;
} VkSubpassDescription;

typedef struct VkSubpassDependency {
    uint32_t srcSubpass; uint32_t dstSubpass;
    VkFlags srcStageMask; VkFlags dstStageMask;
    VkFlags srcAccessMask; VkFlags dstAccessMask;
    VkFlags dependencyFlags;
} VkSubpassDependency;

typedef struct VkRenderPassCreateInfo {
    uint32_t sType; const void* pNext; VkFlags flags;
    uint32_t attachmentCount; const VkAttachmentDescription* pAttachments;
    uint32_t subpassCount; const VkSubpassDescription* pSubpasses;
    uint32_t dependencyCount; const VkSubpassDependency* pDependencies;
} VkRenderPassCreateInfo;

typedef struct VkFramebufferCreateInfo {
    uint32_t sType; const void* pNext; VkFlags flags;
    VkRenderPass renderPass; uint32_t attachmentCount;
    const VkImageView* pAttachments; uint32_t width; uint32_t height; uint32_t layers;
} VkFramebufferCreateInfo;

typedef struct VkClearColorValue { float float32[4]; } VkClearColorValue;
typedef union VkClearValue { VkClearColorValue color; } VkClearValue;

typedef struct VkRenderPassBeginInfo {
    uint32_t sType; const void* pNext; VkRenderPass renderPass;
    VkFramebuffer framebuffer; VkRect2D renderArea;
    uint32_t clearValueCount; const VkClearValue* pClearValues;
} VkRenderPassBeginInfo;

typedef struct VkShaderModuleCreateInfo {
    uint32_t sType; const void* pNext; VkFlags flags;
    size_t codeSize; const uint32_t* pCode;
} VkShaderModuleCreateInfo;

typedef struct VkPipelineShaderStageCreateInfo {
    uint32_t sType; const void* pNext; VkFlags flags;
    VkFlags stage; VkShaderModule module; const char* pName; const void* pSpecializationInfo;
} VkPipelineShaderStageCreateInfo;

typedef struct VkPipelineVertexInputStateCreateInfo {
    uint32_t sType; const void* pNext; VkFlags flags;
    uint32_t vertexBindingDescriptionCount; const void* pVertexBindingDescriptions;
    uint32_t vertexAttributeDescriptionCount; const void* pVertexAttributeDescriptions;
} VkPipelineVertexInputStateCreateInfo;

typedef struct VkPipelineInputAssemblyStateCreateInfo {
    uint32_t sType; const void* pNext; VkFlags flags; uint32_t topology; uint32_t primitiveRestartEnable;
} VkPipelineInputAssemblyStateCreateInfo;

typedef struct VkPipelineViewportStateCreateInfo {
    uint32_t sType; const void* pNext; VkFlags flags;
    uint32_t viewportCount; const VkViewport* pViewports;
    uint32_t scissorCount; const VkRect2D* pScissors;
} VkPipelineViewportStateCreateInfo;

typedef struct VkPipelineRasterizationStateCreateInfo {
    uint32_t sType; const void* pNext; VkFlags flags;
    uint32_t depthClampEnable; uint32_t rasterizerDiscardEnable;
    uint32_t polygonMode; VkFlags cullMode; uint32_t frontFace;
    uint32_t depthBiasEnable; float depthBiasConstantFactor; float depthBiasClamp;
    float depthBiasSlopeFactor; float lineWidth;
} VkPipelineRasterizationStateCreateInfo;

typedef struct VkPipelineMultisampleStateCreateInfo {
    uint32_t sType; const void* pNext; VkFlags flags;
    uint32_t rasterizationSamples; uint32_t sampleShadingEnable; float minSampleShading;
    const void* pSampleMask; uint32_t alphaToCoverageEnable; uint32_t alphaToOneEnable;
} VkPipelineMultisampleStateCreateInfo;

typedef struct VkPipelineColorBlendAttachmentState {
    uint32_t blendEnable; uint32_t srcColorBlendFactor; uint32_t dstColorBlendFactor; uint32_t colorBlendOp;
    uint32_t srcAlphaBlendFactor; uint32_t dstAlphaBlendFactor; uint32_t alphaBlendOp; VkFlags colorWriteMask;
} VkPipelineColorBlendAttachmentState;

typedef struct VkPipelineColorBlendStateCreateInfo {
    uint32_t sType; const void* pNext; VkFlags flags;
    uint32_t logicOpEnable; uint32_t logicOp; uint32_t attachmentCount;
    const VkPipelineColorBlendAttachmentState* pAttachments; float blendConstants[4];
} VkPipelineColorBlendStateCreateInfo;

typedef struct VkPipelineLayoutCreateInfo {
    uint32_t sType; const void* pNext; VkFlags flags;
    uint32_t setLayoutCount; const void* pSetLayouts;
    uint32_t pushConstantRangeCount; const void* pPushConstantRanges;
} VkPipelineLayoutCreateInfo;

typedef struct VkGraphicsPipelineCreateInfo {
    uint32_t sType; const void* pNext; VkFlags flags;
    uint32_t stageCount; const VkPipelineShaderStageCreateInfo* pStages;
    const VkPipelineVertexInputStateCreateInfo* pVertexInputState;
    const VkPipelineInputAssemblyStateCreateInfo* pInputAssemblyState;
    const void* pTessellationState;
    const VkPipelineViewportStateCreateInfo* pViewportState;
    const VkPipelineRasterizationStateCreateInfo* pRasterizationState;
    const VkPipelineMultisampleStateCreateInfo* pMultisampleState;
    const void* pDepthStencilState;
    const VkPipelineColorBlendStateCreateInfo* pColorBlendState;
    const void* pDynamicState;
    VkPipelineLayout layout; VkRenderPass renderPass; uint32_t subpass;
    VkPipeline basePipelineHandle; int32_t basePipelineIndex;
} VkGraphicsPipelineCreateInfo;

typedef struct VkCommandPoolCreateInfo {
    uint32_t sType; const void* pNext; VkFlags flags; uint32_t queueFamilyIndex;
} VkCommandPoolCreateInfo;

typedef struct VkCommandBufferAllocateInfo {
    uint32_t sType; const void* pNext; VkCommandPool commandPool;
    uint32_t level; uint32_t commandBufferCount;
} VkCommandBufferAllocateInfo;

typedef struct VkCommandBufferBeginInfo {
    uint32_t sType; const void* pNext; VkFlags flags; const void* pInheritanceInfo;
} VkCommandBufferBeginInfo;

typedef struct VkSubmitInfo {
    uint32_t sType; const void* pNext; uint32_t waitSemaphoreCount;
    const VkSemaphore* pWaitSemaphores; const VkFlags* pWaitDstStageMask;
    uint32_t commandBufferCount; const VkCommandBuffer* pCommandBuffers;
    uint32_t signalSemaphoreCount; const VkSemaphore* pSignalSemaphores;
} VkSubmitInfo;

typedef struct VkPresentInfoKHR {
    uint32_t sType; const void* pNext; uint32_t waitSemaphoreCount;
    const VkSemaphore* pWaitSemaphores; uint32_t swapchainCount;
    const VkSwapchainKHR* pSwapchains; const uint32_t* pImageIndices;
    VkResult* pResults;
} VkPresentInfoKHR;

typedef struct VkSemaphoreCreateInfo {
    uint32_t sType; const void* pNext; VkFlags flags;
} VkSemaphoreCreateInfo;

typedef void* (*PFN_vkVoidFunction)(void);
typedef PFN_vkVoidFunction (*PFN_vkGetInstanceProcAddr)(VkInstance, const char*);
typedef PFN_vkVoidFunction (*PFN_vkGetDeviceProcAddr)(VkDevice, const char*);

extern PFN_vkVoidFunction vkGetInstanceProcAddr(VkInstance instance, const char* pName);

int kudroid_test_main(void) {
    // Bật Verbose Logging của MoltenVK để bắt mọi cảnh báo MSL/Shader/Pipeline
    setenv("MVK_CONFIG_LOG_LEVEL", "3", 1);
    setenv("MVK_DEBUG", "1", 1);
    setenv("MVK_CONFIG_ACTIVITY_PERFORMANCE_LOGGING", "1", 1);

    LOGI("=================================================");
    LOGI("🔬 [VULKAN POINTER & PIPELINE AUDIT REPORT V2]");
    LOGI("=================================================");

    typedef VkResult (*PFN_vkCreateInstance)(const VkInstanceCreateInfo*, const void*, VkInstance*);
    PFN_vkCreateInstance fnCreateInstance = (PFN_vkCreateInstance)vkGetInstanceProcAddr(NULL, "vkCreateInstance");
    const char* instanceExts[] = { "VK_KHR_surface", "VK_KHR_android_surface" };
    VkApplicationInfo appInfo = { VK_STRUCTURE_TYPE_APPLICATION_INFO, NULL, "VulkanAudit", 1, "KuDroidEngine", 1, 0x00401000 };
    VkInstanceCreateInfo instInfo = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, NULL, 0, &appInfo, 0, NULL, 2, instanceExts };

    VkInstance instance = NULL;
    VkResult instRes = fnCreateInstance(&instInfo, NULL, &instance);
    LOGI("1. Instance: res=%d, instance=%p", (int)instRes, instance);

    void* win = ANativeWindow_fromSurface(NULL, NULL);
    typedef VkResult (*PFN_vkCreateAndroidSurfaceKHR)(VkInstance, const VkAndroidSurfaceCreateInfoKHR*, const void*, VkSurfaceKHR*);
    PFN_vkCreateAndroidSurfaceKHR fnCreateAndroidSurface = (PFN_vkCreateAndroidSurfaceKHR)vkGetInstanceProcAddr(instance, "vkCreateAndroidSurfaceKHR");
    VkAndroidSurfaceCreateInfoKHR surfInfo = { VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR, NULL, 0, win };
    VkSurfaceKHR surface = NULL;
    VkResult surfRes = fnCreateAndroidSurface(instance, &surfInfo, NULL, &surface);
    LOGI("2. Surface: res=%d, surface=%p", (int)surfRes, surface);

    typedef VkResult (*PFN_vkEnumeratePhysicalDevices)(VkInstance, uint32_t*, VkPhysicalDevice*);
    PFN_vkEnumeratePhysicalDevices fnEnumDevs = (PFN_vkEnumeratePhysicalDevices)vkGetInstanceProcAddr(instance, "vkEnumeratePhysicalDevices");
    uint32_t devCount = 1;
    VkPhysicalDevice gpu = NULL;
    fnEnumDevs(instance, &devCount, &gpu);

    // PHÉP ĐO 1: QUEUE FAMILY PROPERTIES & SURFACE SUPPORT
    typedef void (*PFN_vkGetPhysicalDeviceQueueFamilyProperties)(VkPhysicalDevice, uint32_t*, VkQueueFamilyProperties*);
    typedef VkResult (*PFN_vkGetPhysicalDeviceSurfaceSupportKHR)(VkPhysicalDevice, uint32_t, VkSurfaceKHR, VkBool32*);
    PFN_vkGetPhysicalDeviceQueueFamilyProperties fnGetQueueProps = (PFN_vkGetPhysicalDeviceQueueFamilyProperties)vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceQueueFamilyProperties");
    PFN_vkGetPhysicalDeviceSurfaceSupportKHR fnGetSurfaceSupport = (PFN_vkGetPhysicalDeviceSurfaceSupportKHR)vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceSurfaceSupportKHR");

    uint32_t qfCount = 0;
    fnGetQueueProps(gpu, &qfCount, NULL);
    VkQueueFamilyProperties qfProps[8];
    if (qfCount > 8) qfCount = 8;
    fnGetQueueProps(gpu, &qfCount, qfProps);

    LOGI("🔍 [QUEUE FAMILY AUDIT] Total Queue Families Found: %u", qfCount);
    int chosenQueueFamily = -1;
    for (uint32_t i = 0; i < qfCount; ++i) {
        VkBool32 presentSupported = 0;
        if (fnGetSurfaceSupport) {
            fnGetSurfaceSupport(gpu, i, surface, &presentSupported);
        }
        int hasGraphics = (qfProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) ? 1 : 0;
        int hasCompute = (qfProps[i].queueFlags & VK_QUEUE_COMPUTE_BIT) ? 1 : 0;
        int hasTransfer = (qfProps[i].queueFlags & VK_QUEUE_TRANSFER_BIT) ? 1 : 0;
        LOGI("   👉 Family %u: count=%u, flags=0x%08x [Graphics=%d, Compute=%d, Transfer=%d, PresentSupport=%d]",
             i, qfProps[i].queueCount, qfProps[i].queueFlags, hasGraphics, hasCompute, hasTransfer, (int)presentSupported);

        if (hasGraphics && (chosenQueueFamily == -1 || presentSupported)) {
            chosenQueueFamily = (int)i;
        }
    }
    if (chosenQueueFamily == -1) chosenQueueFamily = 0;
    LOGI("   ⭐ Selected Queue Family Index: %d", chosenQueueFamily);

    float qPriorities[] = { 1.0f };
    VkDeviceQueueCreateInfo qInfo = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, NULL, 0, (uint32_t)chosenQueueFamily, 1, qPriorities };
    const char* devExts[] = { "VK_KHR_swapchain" };
    VkDeviceCreateInfo devCreateInfo = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, NULL, 0, 1, &qInfo, 0, NULL, 1, devExts, NULL };

    typedef VkResult (*PFN_vkCreateDevice)(VkPhysicalDevice, const VkDeviceCreateInfo*, const void*, VkDevice*);
    PFN_vkCreateDevice fnCreateDevice = (PFN_vkCreateDevice)vkGetInstanceProcAddr(instance, "vkCreateDevice");
    VkDevice device = NULL;
    VkResult devRes = fnCreateDevice(gpu, &devCreateInfo, NULL, &device);
    LOGI("3. Device: res=%d, device=%p", (int)devRes, device);

    PFN_vkGetDeviceProcAddr fnGetDeviceProcAddr = (PFN_vkGetDeviceProcAddr)vkGetInstanceProcAddr(instance, "vkGetDeviceProcAddr");
    LOGI("4. vkGetDeviceProcAddr fn ptr: %p", (void*)fnGetDeviceProcAddr);

    // Queue
    typedef void (*PFN_vkGetDeviceQueue)(VkDevice, uint32_t, uint32_t, VkQueue*);
    PFN_vkGetDeviceQueue fnGetDeviceQueue = (PFN_vkGetDeviceQueue)vkGetInstanceProcAddr(instance, "vkGetDeviceQueue");
    VkQueue graphicsQueue = NULL;
    fnGetDeviceQueue(device, (uint32_t)chosenQueueFamily, 0, &graphicsQueue);

    // Surface Capabilities
    typedef VkResult (*PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR)(VkPhysicalDevice, VkSurfaceKHR, VkSurfaceCapabilitiesKHR*);
    typedef VkResult (*PFN_vkGetPhysicalDeviceSurfaceFormatsKHR)(VkPhysicalDevice, VkSurfaceKHR, uint32_t*, VkSurfaceFormatKHR*);
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR fnGetCaps = (PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR)vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR fnGetFormats = (PFN_vkGetPhysicalDeviceSurfaceFormatsKHR)vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceSurfaceFormatsKHR");

    VkSurfaceCapabilitiesKHR caps;
    fnGetCaps(gpu, surface, &caps);
    uint32_t formatCount = 8;
    VkSurfaceFormatKHR formats[8];
    fnGetFormats(gpu, surface, &formatCount, formats);
    VkFormat chosenFormat = formats[0].format;
    VkColorSpaceKHR chosenColorSpace = formats[0].colorSpace;

    VkExtent2D swapExtent = caps.currentExtent;
    if (swapExtent.width == 0 || swapExtent.width == 0xFFFFFFFF) {
        swapExtent.width = 828;
        swapExtent.height = 1792;
    }

    // Swapchain
    typedef VkResult (*PFN_vkCreateSwapchainKHR)(VkDevice, const VkSwapchainCreateInfoKHR*, const void*, VkSwapchainKHR*);
    PFN_vkCreateSwapchainKHR fnCreateSwapchain = (PFN_vkCreateSwapchainKHR)vkGetInstanceProcAddr(instance, "vkCreateSwapchainKHR");
    VkSwapchainCreateInfoKHR swInfo = {
        VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, NULL, 0,
        surface, caps.minImageCount > 0 ? caps.minImageCount + 1 : 3,
        chosenFormat, chosenColorSpace, swapExtent, 1,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_SHARING_MODE_EXCLUSIVE,
        0, NULL, caps.currentTransform ? caps.currentTransform : VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR, VK_PRESENT_MODE_FIFO_KHR, 1, NULL
    };
    VkSwapchainKHR swapchain = NULL;
    VkResult swapRes = fnCreateSwapchain(device, &swInfo, NULL, &swapchain);
    LOGI("5. Swapchain: res=%d, swapchain=%p", (int)swapRes, swapchain);

    typedef VkResult (*PFN_vkGetSwapchainImagesKHR)(VkDevice, VkSwapchainKHR, uint32_t*, VkImage*);
    PFN_vkGetSwapchainImagesKHR fnGetSwapchainImages = (PFN_vkGetSwapchainImagesKHR)vkGetInstanceProcAddr(instance, "vkGetSwapchainImagesKHR");
    uint32_t imageCount = 0;
    fnGetSwapchainImages(device, swapchain, &imageCount, NULL);
    VkImage swapImages[8];
    fnGetSwapchainImages(device, swapchain, &imageCount, swapImages);

    typedef VkResult (*PFN_vkCreateImageView)(VkDevice, const VkImageViewCreateInfo*, const void*, VkImageView*);
    PFN_vkCreateImageView fnCreateImageView = (PFN_vkCreateImageView)vkGetInstanceProcAddr(instance, "vkCreateImageView");
    VkImageView swapViews[8];
    for (uint32_t i = 0; i < imageCount; ++i) {
        VkImageViewCreateInfo viewInfo = {
            VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, NULL, 0,
            swapImages[i], VK_IMAGE_VIEW_TYPE_2D, chosenFormat,
            { 0, 0, 0, 0 }, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
        };
        fnCreateImageView(device, &viewInfo, NULL, &swapViews[i]);
    }

    // RenderPass
    typedef VkResult (*PFN_vkCreateRenderPass)(VkDevice, const VkRenderPassCreateInfo*, const void*, VkRenderPass*);
    PFN_vkCreateRenderPass fnCreateRenderPass = (PFN_vkCreateRenderPass)vkGetInstanceProcAddr(instance, "vkCreateRenderPass");
    VkAttachmentDescription colorAttachment = {
        0, chosenFormat, 1,
        VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
        0, 0, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
    };
    VkAttachmentReference colorRef = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkSubpassDescription subpass = { 0, 0, 0, NULL, 1, &colorRef, NULL, NULL, 0, NULL };
    VkSubpassDependency dependency = {
        (~0U) /* VK_SUBPASS_EXTERNAL */, 0,
        0x00000400 /* VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT */,
        0x00000400 /* VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT */,
        0, 0x00000100 /* VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT */, 0
    };
    VkRenderPassCreateInfo rpInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, NULL, 0, 1, &colorAttachment, 1, &subpass, 1, &dependency };
    VkRenderPass renderPass = NULL;
    VkResult rpRes = fnCreateRenderPass(device, &rpInfo, NULL, &renderPass);
    LOGI("6. RenderPass: res=%d, renderPass=%p", (int)rpRes, renderPass);

    typedef VkResult (*PFN_vkCreateFramebuffer)(VkDevice, const VkFramebufferCreateInfo*, const void*, VkFramebuffer*);
    PFN_vkCreateFramebuffer fnCreateFramebuffer = (PFN_vkCreateFramebuffer)vkGetInstanceProcAddr(instance, "vkCreateFramebuffer");
    VkFramebuffer framebuffers[8];
    for (uint32_t i = 0; i < imageCount; ++i) {
        VkFramebufferCreateInfo fbInfo = {
            VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, NULL, 0,
            renderPass, 1, &swapViews[i], swapExtent.width, swapExtent.height, 1
        };
        fnCreateFramebuffer(device, &fbInfo, NULL, &framebuffers[i]);
    }

    // Shaders
    typedef VkResult (*PFN_vkCreateShaderModule)(VkDevice, const VkShaderModuleCreateInfo*, const void*, VkShaderModule*);
    PFN_vkCreateShaderModule fnCreateShaderModule = (PFN_vkCreateShaderModule)vkGetInstanceProcAddr(instance, "vkCreateShaderModule");
    VkShaderModuleCreateInfo vsInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, NULL, 0, kTriangleVertSpirvSize, kTriangleVertSpirv };
    VkShaderModule vertShader = NULL;
    VkResult vsRes = fnCreateShaderModule(device, &vsInfo, NULL, &vertShader);

    VkShaderModuleCreateInfo fsInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, NULL, 0, kTriangleFragSpirvSize, kTriangleFragSpirv };
    VkShaderModule fragShader = NULL;
    VkResult fsRes = fnCreateShaderModule(device, &fsInfo, NULL, &fragShader);
    LOGI("7. Shaders: vsRes=%d (vert=%p, size=%lu), fsRes=%d (frag=%p, size=%lu)",
         (int)vsRes, vertShader, (unsigned long)kTriangleVertSpirvSize, (int)fsRes, fragShader, (unsigned long)kTriangleFragSpirvSize);

    // Layout
    typedef VkResult (*PFN_vkCreatePipelineLayout)(VkDevice, const VkPipelineLayoutCreateInfo*, const void*, VkPipelineLayout*);
    PFN_vkCreatePipelineLayout fnCreatePipelineLayout = (PFN_vkCreatePipelineLayout)vkGetInstanceProcAddr(instance, "vkCreatePipelineLayout");
    VkPipelineLayoutCreateInfo plInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, NULL, 0, 0, NULL, 0, NULL };
    VkPipelineLayout pipelineLayout = NULL;
    VkResult plRes = fnCreatePipelineLayout(device, &plInfo, NULL, &pipelineLayout);
    LOGI("8. PipelineLayout: res=%d, layout=%p", (int)plRes, pipelineLayout);

    // Pipeline
    typedef VkResult (*PFN_vkCreateGraphicsPipelines)(VkDevice, void*, uint32_t, const VkGraphicsPipelineCreateInfo*, const void*, VkPipeline*);
    PFN_vkCreateGraphicsPipelines fnCreateGraphicsPipelines = (PFN_vkCreateGraphicsPipelines)vkGetInstanceProcAddr(instance, "vkCreateGraphicsPipelines");

    VkPipelineShaderStageCreateInfo shaderStages[2] = {
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_VERTEX_BIT, vertShader, "main", NULL },
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragShader, "main", NULL }
    };
    VkPipelineVertexInputStateCreateInfo vertexInputInfo = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, NULL, 0, 0, NULL, 0, NULL };
    VkPipelineInputAssemblyStateCreateInfo inputAssembly = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, NULL, 0, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0 };

    VkViewport viewport = { 0.0f, 0.0f, (float)swapExtent.width, (float)swapExtent.height, 0.0f, 1.0f };
    VkRect2D scissor = { { 0, 0 }, swapExtent };
    VkPipelineViewportStateCreateInfo viewportState = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, NULL, 0, 1, &viewport, 1, &scissor };

    VkPipelineRasterizationStateCreateInfo rasterizer = {
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, NULL, 0,
        0, 0, VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE,
        0, 0.0f, 0.0f, 0.0f, 1.0f
    };
    VkPipelineMultisampleStateCreateInfo multisampling = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, NULL, 0, VK_SAMPLE_COUNT_1_BIT, 0, 1.0f, NULL, 0, 0 };

    // PHÉP ĐO 2: KIỂM TRA COLOR WRITE MASK & ATTACHMENT
    VkPipelineColorBlendAttachmentState colorBlendAttachment = {
        0, 1, 0, 0, 1, 0, 0,
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
    };
    LOGI("🔍 [BLEND AUDIT] colorWriteMask = 0x%x (Expected: 0xf), blendEnable = %u, sizeof(Attachment) = %lu",
         colorBlendAttachment.colorWriteMask, colorBlendAttachment.blendEnable, (unsigned long)sizeof(VkPipelineColorBlendAttachmentState));

    VkPipelineColorBlendStateCreateInfo colorBlending = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, NULL, 0, 0, 0, 1, &colorBlendAttachment, { 0.0f, 0.0f, 0.0f, 0.0f } };

    VkGraphicsPipelineCreateInfo pipelineInfo = {
        VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, NULL, 0,
        2, shaderStages, &vertexInputInfo, &inputAssembly, NULL,
        &viewportState, &rasterizer, &multisampling, NULL,
        &colorBlending, NULL, pipelineLayout, renderPass, 0, NULL, -1
    };
    VkPipeline graphicsPipeline = NULL;
    VkResult pipeRes = fnCreateGraphicsPipelines(device, NULL, 1, &pipelineInfo, NULL, &graphicsPipeline);
    LOGI("9. GraphicsPipeline: pipeRes=%d, pipeline=%p", (int)pipeRes, graphicsPipeline);

    // Command Pool
    typedef VkResult (*PFN_vkCreateCommandPool)(VkDevice, const VkCommandPoolCreateInfo*, const void*, VkCommandPool*);
    PFN_vkCreateCommandPool fnCreateCommandPool = (PFN_vkCreateCommandPool)vkGetInstanceProcAddr(instance, "vkCreateCommandPool");
    VkCommandPoolCreateInfo cpInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, NULL, 0x00000002, (uint32_t)chosenQueueFamily };
    VkCommandPool commandPool = NULL;
    fnCreateCommandPool(device, &cpInfo, NULL, &commandPool);

    typedef VkResult (*PFN_vkAllocateCommandBuffers)(VkDevice, const VkCommandBufferAllocateInfo*, VkCommandBuffer*);
    PFN_vkAllocateCommandBuffers fnAllocCmds = (PFN_vkAllocateCommandBuffers)vkGetInstanceProcAddr(instance, "vkAllocateCommandBuffers");
    VkCommandBufferAllocateInfo cbAlloc = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, NULL, commandPool, 0, imageCount };
    VkCommandBuffer cmdBuffers[8];
    fnAllocCmds(device, &cbAlloc, cmdBuffers);

    typedef VkResult (*PFN_vkResetCommandBuffer)(VkCommandBuffer, VkFlags);
    typedef VkResult (*PFN_vkBeginCommandBuffer)(VkCommandBuffer, const VkCommandBufferBeginInfo*);
    typedef void (*PFN_vkCmdBeginRenderPass)(VkCommandBuffer, const VkRenderPassBeginInfo*, uint32_t);
    typedef void (*PFN_vkCmdBindPipeline)(VkCommandBuffer, uint32_t, VkPipeline);
    typedef void (*PFN_vkCmdDraw)(VkCommandBuffer, uint32_t, uint32_t, uint32_t, uint32_t);
    typedef void (*PFN_vkCmdEndRenderPass)(VkCommandBuffer);
    typedef VkResult (*PFN_vkEndCommandBuffer)(VkCommandBuffer);
    typedef VkResult (*PFN_vkQueueSubmit)(VkQueue, uint32_t, const VkSubmitInfo*, VkFence);
    typedef VkResult (*PFN_vkQueueWaitIdle)(VkQueue);
    typedef VkResult (*PFN_vkAcquireNextImageKHR)(VkDevice, VkSwapchainKHR, uint64_t, VkSemaphore, VkFence, uint32_t*);
    typedef VkResult (*PFN_vkQueuePresentKHR)(VkQueue, const VkPresentInfoKHR*);
    typedef VkResult (*PFN_vkCreateSemaphore)(VkDevice, const VkSemaphoreCreateInfo*, const void*, VkSemaphore*);

    PFN_vkResetCommandBuffer fnResetCmd = (PFN_vkResetCommandBuffer)(fnGetDeviceProcAddr ? fnGetDeviceProcAddr(device, "vkResetCommandBuffer") : vkGetInstanceProcAddr(instance, "vkResetCommandBuffer"));
    PFN_vkBeginCommandBuffer fnBeginCmd = (PFN_vkBeginCommandBuffer)(fnGetDeviceProcAddr ? fnGetDeviceProcAddr(device, "vkBeginCommandBuffer") : vkGetInstanceProcAddr(instance, "vkBeginCommandBuffer"));
    PFN_vkCmdBeginRenderPass fnCmdBeginRenderPass = (PFN_vkCmdBeginRenderPass)(fnGetDeviceProcAddr ? fnGetDeviceProcAddr(device, "vkCmdBeginRenderPass") : vkGetInstanceProcAddr(instance, "vkCmdBeginRenderPass"));
    PFN_vkCmdBindPipeline fnCmdBindPipeline = (PFN_vkCmdBindPipeline)(fnGetDeviceProcAddr ? fnGetDeviceProcAddr(device, "vkCmdBindPipeline") : vkGetInstanceProcAddr(instance, "vkCmdBindPipeline"));
    PFN_vkCmdDraw fnCmdDraw = (PFN_vkCmdDraw)(fnGetDeviceProcAddr ? fnGetDeviceProcAddr(device, "vkCmdDraw") : vkGetInstanceProcAddr(instance, "vkCmdDraw"));
    PFN_vkCmdEndRenderPass fnCmdEndRenderPass = (PFN_vkCmdEndRenderPass)(fnGetDeviceProcAddr ? fnGetDeviceProcAddr(device, "vkCmdEndRenderPass") : vkGetInstanceProcAddr(instance, "vkCmdEndRenderPass"));
    PFN_vkEndCommandBuffer fnEndCmd = (PFN_vkEndCommandBuffer)(fnGetDeviceProcAddr ? fnGetDeviceProcAddr(device, "vkEndCommandBuffer") : vkGetInstanceProcAddr(instance, "vkEndCommandBuffer"));
    PFN_vkQueueSubmit fnQueueSubmit = (PFN_vkQueueSubmit)(fnGetDeviceProcAddr ? fnGetDeviceProcAddr(device, "vkQueueSubmit") : vkGetInstanceProcAddr(instance, "vkQueueSubmit"));
    PFN_vkQueueWaitIdle fnQueueWaitIdle = (PFN_vkQueueWaitIdle)(fnGetDeviceProcAddr ? fnGetDeviceProcAddr(device, "vkQueueWaitIdle") : vkGetInstanceProcAddr(instance, "vkQueueWaitIdle"));
    PFN_vkAcquireNextImageKHR fnAcquireNextImage = (PFN_vkAcquireNextImageKHR)(fnGetDeviceProcAddr ? fnGetDeviceProcAddr(device, "vkAcquireNextImageKHR") : vkGetInstanceProcAddr(instance, "vkAcquireNextImageKHR"));
    PFN_vkQueuePresentKHR fnQueuePresent = (PFN_vkQueuePresentKHR)(fnGetDeviceProcAddr ? fnGetDeviceProcAddr(device, "vkQueuePresentKHR") : vkGetInstanceProcAddr(instance, "vkQueuePresentKHR"));
    PFN_vkCreateSemaphore fnCreateSemaphore = (PFN_vkCreateSemaphore)(fnGetDeviceProcAddr ? fnGetDeviceProcAddr(device, "vkCreateSemaphore") : vkGetInstanceProcAddr(instance, "vkCreateSemaphore"));

    VkSemaphoreCreateInfo semInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, NULL, 0 };
    VkSemaphore imageAvailableSemaphore = NULL;
    VkSemaphore renderFinishedSemaphore = NULL;
    fnCreateSemaphore(device, &semInfo, NULL, &imageAvailableSemaphore);
    fnCreateSemaphore(device, &semInfo, NULL, &renderFinishedSemaphore);

    LOGI("10. Starting Audit V2 Render Loop (600 frames)...");

    for (int frame = 0; frame < 600; ++frame) {
        uint32_t imageIndex = 0;
        VkResult acqRes = fnAcquireNextImage(device, swapchain, 1000000000ULL, imageAvailableSemaphore, NULL, &imageIndex);

        VkCommandBuffer cmd = cmdBuffers[imageIndex];
        if (fnResetCmd) fnResetCmd(cmd, 0);

        VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, NULL, 1, NULL };
        VkResult beginRes = fnBeginCmd ? fnBeginCmd(cmd, &beginInfo) : -99;

        VkClearValue clearColor = { { { 0.12f, 0.08f, 0.20f, 1.0f } } };
        VkRenderPassBeginInfo rpBegin = {
            VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, NULL,
            renderPass, framebuffers[imageIndex],
            { { 0, 0 }, swapExtent },
            1, &clearColor
        };

        if (fnCmdBeginRenderPass) fnCmdBeginRenderPass(cmd, &rpBegin, 0);
        if (fnCmdBindPipeline && graphicsPipeline) fnCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
        if (fnCmdDraw) fnCmdDraw(cmd, 3, 1, 0, 0);
        if (fnCmdEndRenderPass) fnCmdEndRenderPass(cmd);

        VkResult endRes = fnEndCmd ? fnEndCmd(cmd) : -99;

        VkFlags waitStages = 0x00000400;
        VkSubmitInfo submitInfo = {
            VK_STRUCTURE_TYPE_SUBMIT_INFO, NULL,
            1, &imageAvailableSemaphore, &waitStages,
            1, &cmd,
            1, &renderFinishedSemaphore
        };
        VkResult submitRes = fnQueueSubmit ? fnQueueSubmit(graphicsQueue, 1, &submitInfo, NULL) : -99;

        VkPresentInfoKHR presentInfo = {
            VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, NULL,
            1, &renderFinishedSemaphore,
            1, &swapchain, &imageIndex, NULL
        };
        VkResult presRes = fnQueuePresent ? fnQueuePresent(graphicsQueue, &presentInfo) : -99;

        if (frame == 0) {
            LOGI("🔬 [FRAME 0 AUDIT V2] acqRes=%d, beginRes=%d, endRes=%d, submitRes=%d, presRes=%d",
                 (int)acqRes, (int)beginRes, (int)endRes, (int)submitRes, (int)presRes);
        }

        if (fnQueueWaitIdle) fnQueueWaitIdle(graphicsQueue);
        usleep(16666);
    }

    LOGI("=================================================");
    LOGI("✔ VULKAN AUDIT V2 REPORT COMPLETED!");
    LOGI("=================================================");
    return 0;
}
