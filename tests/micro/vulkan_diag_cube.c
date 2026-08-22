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
#define LOG_TAG "VulkanDiag"
#define LOGI(...) __android_log_print(3, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(6, LOG_TAG, __VA_ARGS__)

#include "cube_vert_spv.h"
#include "cube_frag_spv.h"

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
#define VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO 27
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

typedef struct VkRenderPassCreateInfo {
    uint32_t sType; const void* pNext; VkFlags flags;
    uint32_t attachmentCount; const VkAttachmentDescription* pAttachments;
    uint32_t subpassCount; const VkSubpassDescription* pSubpasses;
    uint32_t dependencyCount; const void* pDependencies;
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

typedef struct VkPipelineDynamicStateCreateInfo {
    uint32_t sType; const void* pNext; VkFlags flags; uint32_t dynamicStateCount; const uint32_t* pDynamicStates;
} VkPipelineDynamicStateCreateInfo;

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
    const VkPipelineDynamicStateCreateInfo* pDynamicState;
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
typedef VkResult (*PFN_vkCreateInstance)(const VkInstanceCreateInfo*, const void*, VkInstance*);
typedef VkResult (*PFN_vkEnumeratePhysicalDevices)(VkInstance, uint32_t*, VkPhysicalDevice*);
typedef VkResult (*PFN_vkCreateDevice)(VkPhysicalDevice, const VkDeviceCreateInfo*, const void*, VkDevice*);
typedef void (*PFN_vkGetDeviceQueue)(VkDevice, uint32_t, uint32_t, VkQueue*);
typedef VkResult (*PFN_vkCreateAndroidSurfaceKHR)(VkInstance, const VkAndroidSurfaceCreateInfoKHR*, const void*, VkSurfaceKHR*);
typedef VkResult (*PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR)(VkPhysicalDevice, VkSurfaceKHR, VkSurfaceCapabilitiesKHR*);
typedef VkResult (*PFN_vkGetPhysicalDeviceSurfaceFormatsKHR)(VkPhysicalDevice, VkSurfaceKHR, uint32_t*, VkSurfaceFormatKHR*);
typedef VkResult (*PFN_vkCreateSwapchainKHR)(VkDevice, const VkSwapchainCreateInfoKHR*, const void*, VkSwapchainKHR*);
typedef VkResult (*PFN_vkGetSwapchainImagesKHR)(VkDevice, VkSwapchainKHR, uint32_t*, VkImage*);
typedef VkResult (*PFN_vkCreateImageView)(VkDevice, const VkImageViewCreateInfo*, const void*, VkImageView*);
typedef VkResult (*PFN_vkCreateRenderPass)(VkDevice, const VkRenderPassCreateInfo*, const void*, VkRenderPass*);
typedef VkResult (*PFN_vkCreateFramebuffer)(VkDevice, const VkFramebufferCreateInfo*, const void*, VkFramebuffer*);
typedef VkResult (*PFN_vkCreateShaderModule)(VkDevice, const VkShaderModuleCreateInfo*, const void*, VkShaderModule*);
typedef VkResult (*PFN_vkCreatePipelineLayout)(VkDevice, const VkPipelineLayoutCreateInfo*, const void*, VkPipelineLayout*);
typedef VkResult (*PFN_vkCreateGraphicsPipelines)(VkDevice, void*, uint32_t, const VkGraphicsPipelineCreateInfo*, const void*, VkPipeline*);
typedef VkResult (*PFN_vkCreateCommandPool)(VkDevice, const VkCommandPoolCreateInfo*, const void*, VkCommandPool*);
typedef VkResult (*PFN_vkAllocateCommandBuffers)(VkDevice, const VkCommandBufferAllocateInfo*, VkCommandBuffer*);
typedef VkResult (*PFN_vkBeginCommandBuffer)(VkCommandBuffer, const VkCommandBufferBeginInfo*);
typedef void (*PFN_vkCmdBeginRenderPass)(VkCommandBuffer, const VkRenderPassBeginInfo*, uint32_t);
typedef void (*PFN_vkCmdBindPipeline)(VkCommandBuffer, uint32_t, VkPipeline);
typedef void (*PFN_vkCmdSetViewport)(VkCommandBuffer, uint32_t, uint32_t, const VkViewport*);
typedef void (*PFN_vkCmdSetScissor)(VkCommandBuffer, uint32_t, uint32_t, const VkRect2D*);
typedef void (*PFN_vkCmdDraw)(VkCommandBuffer, uint32_t, uint32_t, uint32_t, uint32_t);
typedef void (*PFN_vkCmdEndRenderPass)(VkCommandBuffer);
typedef VkResult (*PFN_vkEndCommandBuffer)(VkCommandBuffer);
typedef VkResult (*PFN_vkQueueSubmit)(VkQueue, uint32_t, const VkSubmitInfo*, VkFence);
typedef VkResult (*PFN_vkAcquireNextImageKHR)(VkDevice, VkSwapchainKHR, uint64_t, VkSemaphore, VkFence, uint32_t*);
typedef VkResult (*PFN_vkQueuePresentKHR)(VkQueue, const VkPresentInfoKHR*);
typedef VkResult (*PFN_vkCreateSemaphore)(VkDevice, const VkSemaphoreCreateInfo*, const void*, VkSemaphore*);

extern PFN_vkVoidFunction vkGetInstanceProcAddr(VkInstance instance, const char* pName);

int kudroid_test_main(void) {
    LOGI("=================================================");
    LOGI("🔍 [VULKAN 1.2 GRAPHICS PIPELINE DIAGNOSTIC]");
    LOGI("=================================================");

    void* win = ANativeWindow_fromSurface(NULL, NULL);
    PFN_vkCreateInstance fnCreateInstance = (PFN_vkCreateInstance)vkGetInstanceProcAddr(NULL, "vkCreateInstance");
    const char* instanceExts[] = { "VK_KHR_surface", "VK_KHR_android_surface" };
    VkApplicationInfo appInfo = { VK_STRUCTURE_TYPE_APPLICATION_INFO, NULL, "VulkanDiag", 1, "KuDroidEngine", 1, 0x00402000 };
    VkInstanceCreateInfo instInfo = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, NULL, 0, &appInfo, 0, NULL, 2, instanceExts };

    VkInstance instance = NULL;
    VkResult res = fnCreateInstance(&instInfo, NULL, &instance);
    LOGI("Stage 1 (Instance): res=%d, instance=%p", (int)res, instance);

    PFN_vkCreateAndroidSurfaceKHR fnCreateAndroidSurface = (PFN_vkCreateAndroidSurfaceKHR)vkGetInstanceProcAddr(instance, "vkCreateAndroidSurfaceKHR");
    VkAndroidSurfaceCreateInfoKHR surfInfo = { VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR, NULL, 0, win };
    VkSurfaceKHR surface = NULL;
    res = fnCreateAndroidSurface(instance, &surfInfo, NULL, &surface);
    LOGI("Stage 2 (Surface): res=%d, surface=%p", (int)res, surface);

    PFN_vkEnumeratePhysicalDevices fnEnumDevs = (PFN_vkEnumeratePhysicalDevices)vkGetInstanceProcAddr(instance, "vkEnumeratePhysicalDevices");
    uint32_t devCount = 1;
    VkPhysicalDevice gpu = NULL;
    fnEnumDevs(instance, &devCount, &gpu);

    float qPriorities[] = { 1.0f };
    VkDeviceQueueCreateInfo qInfo = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, NULL, 0, 0, 1, qPriorities };
    const char* devExts[] = { "VK_KHR_swapchain" };
    VkDeviceCreateInfo devCreateInfo = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, NULL, 0, 1, &qInfo, 0, NULL, 1, devExts, NULL };

    PFN_vkCreateDevice fnCreateDevice = (PFN_vkCreateDevice)vkGetInstanceProcAddr(instance, "vkCreateDevice");
    VkDevice device = NULL;
    res = fnCreateDevice(gpu, &devCreateInfo, NULL, &device);
    LOGI("Stage 3 (Device): res=%d, device=%p", (int)res, device);

    PFN_vkGetDeviceQueue fnGetDeviceQueue = (PFN_vkGetDeviceQueue)vkGetInstanceProcAddr(instance, "vkGetDeviceQueue");
    VkQueue graphicsQueue = NULL;
    fnGetDeviceQueue(device, 0, 0, &graphicsQueue);

    // Query Surface Capabilities
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR fnGetCaps = (PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR)vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    VkSurfaceCapabilitiesKHR caps;
    fnGetCaps(gpu, surface, &caps);
    LOGI("Stage 4 (Caps): Extent=%ux%u, minImg=%u, maxImg=%u", caps.currentExtent.width, caps.currentExtent.height, caps.minImageCount, caps.maxImageCount);

    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR fnGetFormats = (PFN_vkGetPhysicalDeviceSurfaceFormatsKHR)vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceSurfaceFormatsKHR");
    uint32_t formatCount = 8;
    VkSurfaceFormatKHR formats[8];
    fnGetFormats(gpu, surface, &formatCount, formats);
    VkFormat chosenFormat = formats[0].format;
    VkColorSpaceKHR chosenColorSpace = formats[0].colorSpace;
    LOGI("Stage 5 (Format): chosen=%u, space=%u", chosenFormat, chosenColorSpace);

    VkExtent2D swapExtent = caps.currentExtent;
    if (swapExtent.width == 0 || swapExtent.width == 0xFFFFFFFF) {
        swapExtent.width = 828;
        swapExtent.height = 1792;
    }

    VkSwapchainCreateInfoKHR swInfo = {
        VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, NULL, 0,
        surface, caps.minImageCount > 0 ? caps.minImageCount + 1 : 3,
        chosenFormat, chosenColorSpace, swapExtent, 1,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_SHARING_MODE_EXCLUSIVE,
        0, NULL, caps.currentTransform ? caps.currentTransform : VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR, VK_PRESENT_MODE_FIFO_KHR, 1, NULL
    };
    PFN_vkCreateSwapchainKHR fnCreateSwapchain = (PFN_vkCreateSwapchainKHR)vkGetInstanceProcAddr(instance, "vkCreateSwapchainKHR");
    VkSwapchainKHR swapchain = NULL;
    res = fnCreateSwapchain(device, &swInfo, NULL, &swapchain);
    LOGI("Stage 6 (Swapchain): res=%d, swapchain=%p", (int)res, swapchain);

    PFN_vkGetSwapchainImagesKHR fnGetSwapchainImages = (PFN_vkGetSwapchainImagesKHR)vkGetInstanceProcAddr(instance, "vkGetSwapchainImagesKHR");
    uint32_t imageCount = 0;
    fnGetSwapchainImages(device, swapchain, &imageCount, NULL);
    VkImage swapImages[8];
    fnGetSwapchainImages(device, swapchain, &imageCount, swapImages);
    LOGI("Stage 7 (Images): count=%u", imageCount);

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

    // Render Pass matched with chosenFormat
    VkAttachmentDescription colorAttachment = {
        0, chosenFormat, 1,
        VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
        0, 0, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
    };
    VkAttachmentReference colorRef = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkSubpassDescription subpass = { 0, 0, 0, NULL, 1, &colorRef, NULL, NULL, 0, NULL };
    VkRenderPassCreateInfo rpInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, NULL, 0, 1, &colorAttachment, 1, &subpass, 0, NULL };

    PFN_vkCreateRenderPass fnCreateRenderPass = (PFN_vkCreateRenderPass)vkGetInstanceProcAddr(instance, "vkCreateRenderPass");
    VkRenderPass renderPass = NULL;
    res = fnCreateRenderPass(device, &rpInfo, NULL, &renderPass);
    LOGI("Stage 8 (RenderPass): res=%d, renderPass=%p", (int)res, renderPass);

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
    PFN_vkCreateShaderModule fnCreateShaderModule = (PFN_vkCreateShaderModule)vkGetInstanceProcAddr(instance, "vkCreateShaderModule");
    VkShaderModuleCreateInfo vsInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, NULL, 0, kCubeVertSpirvSize, kCubeVertSpirv };
    VkShaderModule vertShader = NULL;
    VkResult vsRes = fnCreateShaderModule(device, &vsInfo, NULL, &vertShader);

    VkShaderModuleCreateInfo fsInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, NULL, 0, kCubeFragSpirvSize, kCubeFragSpirv };
    VkShaderModule fragShader = NULL;
    VkResult fsRes = fnCreateShaderModule(device, &fsInfo, NULL, &fragShader);
    LOGI("Stage 9 (Shaders): vsRes=%d (vert=%p), fsRes=%d (frag=%p)", (int)vsRes, vertShader, (int)fsRes, fragShader);

    // Pipeline Layout
    VkPipelineLayoutCreateInfo plInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, NULL, 0, 0, NULL, 0, NULL };
    PFN_vkCreatePipelineLayout fnCreatePipelineLayout = (PFN_vkCreatePipelineLayout)vkGetInstanceProcAddr(instance, "vkCreatePipelineLayout");
    VkPipelineLayout pipelineLayout = NULL;
    res = fnCreatePipelineLayout(device, &plInfo, NULL, &pipelineLayout);
    LOGI("Stage 10 (PipelineLayout): res=%d, layout=%p", (int)res, pipelineLayout);

    // Graphics Pipeline
    VkPipelineShaderStageCreateInfo shaderStages[2] = {
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_VERTEX_BIT, vertShader, "main", NULL },
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragShader, "main", NULL }
    };

    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, NULL, 0, 0, NULL, 0, NULL
    };

    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, NULL, 0,
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0
    };

    VkViewport viewport = { 0.0f, 0.0f, (float)swapExtent.width, (float)swapExtent.height, 0.0f, 1.0f };
    VkRect2D scissor = { { 0, 0 }, swapExtent };
    VkPipelineViewportStateCreateInfo viewportState = {
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, NULL, 0,
        1, &viewport, 1, &scissor
    };

    VkPipelineRasterizationStateCreateInfo rasterizer = {
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, NULL, 0,
        0, 0, VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE,
        0, 0.0f, 0.0f, 0.0f, 1.0f
    };

    VkPipelineMultisampleStateCreateInfo multisampling = {
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, NULL, 0,
        VK_SAMPLE_COUNT_1_BIT, 0, 1.0f, NULL, 0, 0
    };

    VkPipelineColorBlendAttachmentState colorBlendAttachment = {
        0, 1, 0, 0, 1, 0, 0,
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
    };

    VkPipelineColorBlendStateCreateInfo colorBlending = {
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, NULL, 0,
        0, 0, 1, &colorBlendAttachment, { 0.0f, 0.0f, 0.0f, 0.0f }
    };

    VkGraphicsPipelineCreateInfo pipelineInfo = {
        VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, NULL, 0,
        2, shaderStages, &vertexInputInfo, &inputAssembly, NULL,
        &viewportState, &rasterizer, &multisampling, NULL,
        &colorBlending, NULL, pipelineLayout, renderPass, 0, NULL, -1
    };

    PFN_vkCreateGraphicsPipelines fnCreateGraphicsPipelines = (PFN_vkCreateGraphicsPipelines)vkGetInstanceProcAddr(instance, "vkCreateGraphicsPipelines");
    VkPipeline graphicsPipeline = NULL;
    VkResult pipeRes = fnCreateGraphicsPipelines(device, NULL, 1, &pipelineInfo, NULL, &graphicsPipeline);
    LOGI("Stage 11 (GraphicsPipeline): pipeRes=%d, pipeline=%p", (int)pipeRes, graphicsPipeline);

    // Command Pool & Buffers
    PFN_vkCreateCommandPool fnCreateCommandPool = (PFN_vkCreateCommandPool)vkGetInstanceProcAddr(instance, "vkCreateCommandPool");
    VkCommandPoolCreateInfo cpInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, NULL, 0x00000002, 0 };
    VkCommandPool commandPool = NULL;
    fnCreateCommandPool(device, &cpInfo, NULL, &commandPool);

    PFN_vkAllocateCommandBuffers fnAllocCmds = (PFN_vkAllocateCommandBuffers)vkGetInstanceProcAddr(instance, "vkAllocateCommandBuffers");
    VkCommandBufferAllocateInfo cbAlloc = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, NULL, commandPool, 0, imageCount };
    VkCommandBuffer cmdBuffers[8];
    fnAllocCmds(device, &cbAlloc, cmdBuffers);

    PFN_vkBeginCommandBuffer fnBeginCmd = (PFN_vkBeginCommandBuffer)vkGetInstanceProcAddr(instance, "vkBeginCommandBuffer");
    PFN_vkCmdBeginRenderPass fnCmdBeginRenderPass = (PFN_vkCmdBeginRenderPass)vkGetInstanceProcAddr(instance, "vkCmdBeginRenderPass");
    PFN_vkCmdBindPipeline fnCmdBindPipeline = (PFN_vkCmdBindPipeline)vkGetInstanceProcAddr(instance, "vkCmdBindPipeline");
    PFN_vkCmdDraw fnCmdDraw = (PFN_vkCmdDraw)vkGetInstanceProcAddr(instance, "vkCmdDraw");
    PFN_vkCmdEndRenderPass fnCmdEndRenderPass = (PFN_vkCmdEndRenderPass)vkGetInstanceProcAddr(instance, "vkCmdEndRenderPass");
    PFN_vkEndCommandBuffer fnEndCmd = (PFN_vkEndCommandBuffer)vkGetInstanceProcAddr(instance, "vkEndCommandBuffer");
    PFN_vkQueueSubmit fnQueueSubmit = (PFN_vkQueueSubmit)vkGetInstanceProcAddr(instance, "vkQueueSubmit");
    PFN_vkAcquireNextImageKHR fnAcquireNextImage = (PFN_vkAcquireNextImageKHR)vkGetInstanceProcAddr(instance, "vkAcquireNextImageKHR");
    PFN_vkQueuePresentKHR fnQueuePresent = (PFN_vkQueuePresentKHR)vkGetInstanceProcAddr(instance, "vkQueuePresentKHR");

    PFN_vkCreateSemaphore fnCreateSemaphore = (PFN_vkCreateSemaphore)vkGetInstanceProcAddr(instance, "vkCreateSemaphore");
    VkSemaphoreCreateInfo semInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, NULL, 0 };
    VkSemaphore imageAvailableSemaphore = NULL;
    VkSemaphore renderFinishedSemaphore = NULL;
    if (fnCreateSemaphore) {
        fnCreateSemaphore(device, &semInfo, NULL, &imageAvailableSemaphore);
        fnCreateSemaphore(device, &semInfo, NULL, &renderFinishedSemaphore);
    }

    LOGI("Stage 12 (Present Loop): Starting Diagnostic Draw (300 frames)...");

    for (int frame = 0; frame < 300; ++frame) {
        uint32_t imageIndex = 0;
        VkResult acqRes = fnAcquireNextImage(device, swapchain, 1000000000ULL, imageAvailableSemaphore, NULL, &imageIndex);

        VkCommandBuffer cmd = cmdBuffers[imageIndex];
        VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, NULL, 1, NULL };
        fnBeginCmd(cmd, &beginInfo);

        VkClearValue clearColor = { { { 0.15f, 0.05f, 0.25f, 1.0f } } }; // Deep Purple
        VkRenderPassBeginInfo rpBegin = {
            VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, NULL,
            renderPass, framebuffers[imageIndex],
            { { 0, 0 }, swapExtent },
            1, &clearColor
        };

        fnCmdBeginRenderPass(cmd, &rpBegin, 0);
        if (graphicsPipeline) {
            fnCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
            fnCmdDraw(cmd, 3, 1, 0, 0);
        }
        fnCmdEndRenderPass(cmd);
        fnEndCmd(cmd);

        VkFlags waitStages = 0x00000400;
        VkSubmitInfo submitInfo = {
            VK_STRUCTURE_TYPE_SUBMIT_INFO, NULL,
            1, &imageAvailableSemaphore, &waitStages,
            1, &cmd,
            1, &renderFinishedSemaphore
        };
        fnQueueSubmit(graphicsQueue, 1, &submitInfo, NULL);

        VkPresentInfoKHR presentInfo = {
            VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, NULL,
            1, &renderFinishedSemaphore,
            1, &swapchain, &imageIndex, NULL
        };
        VkResult presRes = fnQueuePresent(graphicsQueue, &presentInfo);

        if (frame == 0) {
            LOGI("Frame 0 Diagnostic: acqRes=%d, presRes=%d, imageIndex=%u", (int)acqRes, (int)presRes, imageIndex);
        }

        usleep(16666);
    }

    LOGI("=================================================");
    LOGI("✔ VULKAN DIAGNOSTIC TEST FINISHED!");
    LOGI("=================================================");
    return 0;
}
