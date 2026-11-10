
// ─────────────────────────────────────────────────────────────────────────────
// Vulkan ↔ MoltenVK translation
//
// Android games request the Android-specific surface extension
// (VK_KHR_android_surface / vkCreateAndroidSurfaceKHR). MoltenVK on iOS only
// provides VK_EXT_metal_surface / vkCreateMetalSurfaceEXT. We intercept
// vkGetInstanceProcAddr and translate the surface creation call.
//
// Because bionic_ANativeWindow_fromSurface returns g_metalLayer (a CAMetalLayer)
// directly, the "window" pointer the game passes IS the CAMetalLayer — no
// struct unwrapping needed.
// ─────────────────────────────────────────────────────────────────────────────

// Minimal Vulkan types (opaque handles + result codes).
typedef uint32_t VkResult;
typedef uint32_t VkFlags;
typedef void* VkInstance;
typedef void* VkSurfaceKHR;
typedef void* VkAllocationCallbacks;
typedef void* PFN_vkVoidFunction;
typedef void* VkDevice;

extern "C" PFN_vkVoidFunction bionic_vkGetDeviceProcAddr(VkDevice device, const char* pName);

#define VK_SUCCESS 0
#define VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT 1000217000
#define VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR 1000008000

typedef struct VkAndroidSurfaceCreateInfoKHR {
    uint32_t sType;
    const void* pNext;
    VkFlags flags;
    void* window; // ANativeWindow* — actually our CAMetalLayer
} VkAndroidSurfaceCreateInfoKHR;

typedef struct VkMetalSurfaceCreateInfoEXT {
    uint32_t sType;
    const void* pNext;
    VkFlags flags;
    void* pLayer; // CAMetalLayer*
} VkMetalSurfaceCreateInfoEXT;

typedef struct VkInstanceCreateInfo {
    uint32_t sType;
    const void* pNext;
    VkFlags flags;
    const void* pApplicationInfo;
    uint32_t enabledLayerCount;
    const char* const* ppEnabledLayerNames;
    uint32_t enabledExtensionCount;
    const char* const* ppEnabledExtensionNames;
} VkInstanceCreateInfo;

#define VK_NULL_HANDLE nullptr

typedef struct VkExtensionProperties {
    char extensionName[256];
    uint32_t specVersion;
} VkExtensionProperties;

#define VK_STRUCTURE_TYPE_IOS_SURFACE_CREATE_INFO_MVK 1000122000
#define VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT 1000217000

typedef struct VkIOSSurfaceCreateInfoMVK {
    uint32_t sType;
    const void* pNext;
    uint32_t flags;
    const void* pView;
} VkIOSSurfaceCreateInfoMVK;

typedef VkResult (*PFN_vkCreateInstance)(const VkInstanceCreateInfo*, const VkAllocationCallbacks*, VkInstance*);
typedef PFN_vkVoidFunction (*PFN_vkGetInstanceProcAddr)(VkInstance, const char*);
typedef VkResult (*PFN_vkEnumerateInstanceExtensionProperties)(const char*, uint32_t*, void*);
typedef VkResult (*PFN_vkCreateIOSSurfaceMVK)(VkInstance, const VkIOSSurfaceCreateInfoMVK*, const VkAllocationCallbacks*, VkSurfaceKHR*);
typedef VkResult (*PFN_vkCreateMetalSurfaceEXT)(VkInstance, const VkMetalSurfaceCreateInfoEXT*, const VkAllocationCallbacks*, VkSurfaceKHR*);

extern "C" VkResult bionic_vkEnumerateInstanceExtensionProperties(const char* pLayerName,
                                                                  uint32_t* pPropertyCount,
                                                                  VkExtensionProperties* pProperties);

static void* get_mvk_handle() {
    static void* mvk = nullptr;
    if (!mvk) {
        mvk = dlopen_bundled_framework("MoltenVK.framework/MoltenVK",
                                       RTLD_NOW | RTLD_GLOBAL);
    }
    return mvk;
}

static PFN_vkGetInstanceProcAddr get_real_vkGetInstanceProcAddr() {
    static PFN_vkGetInstanceProcAddr real_gipa = nullptr;
    if (!real_gipa) {
        void* mvk = get_mvk_handle();
        if (mvk) {
            real_gipa = (PFN_vkGetInstanceProcAddr)::dlsym(mvk, "vkGetInstanceProcAddr");
        }
    }
    return real_gipa;
}

static VkInstance s_activeVkInstance = nullptr;

// One table drives all three resolvers (instance-proc, device-proc, raw dlsym):
// per-name resolve-then-wrap blocks drifted apart, which is how swapchain
// creation stayed invisible for weeks.
typedef uint32_t (*PFN_vkSurfaceCaps_fn)(void*, void*, void*);
typedef uint32_t (*PFN_vkCreateSwapchainKHR_fn)(void*, const void*, const void*, void**);
typedef uint32_t (*PFN_vkQueueSubmit_fn)(void*, uint32_t, const void*, void*);
typedef uint32_t (*PFN_vkQueuePresentKHR_fn)(void*, const void*);
typedef uint32_t (*PFN_vkAcquireNextImageKHR_fn)(void*, void*, uint64_t, void*, void*,
                                                 uint32_t*);
static PFN_vkSurfaceCaps_fn s_realSurfaceCaps = nullptr;
static PFN_vkCreateSwapchainKHR_fn s_realCreateSwapchain = nullptr;
static PFN_vkQueueSubmit_fn s_realQueueSubmit = nullptr;
static PFN_vkQueuePresentKHR_fn s_realQueuePresent = nullptr;
static PFN_vkAcquireNextImageKHR_fn s_realAcquireNextImage = nullptr;
extern "C" uint32_t bionic_vkSurfaceCaps(void* phys, void* surface, void* caps);
extern "C" uint32_t bionic_vkQueueSubmit(void* queue, uint32_t submit_count,
                                         const void* submits, void* fence);
extern "C" uint32_t bionic_vkQueuePresentKHR(void* queue, const void* present_info);
extern "C" uint32_t bionic_vkAcquireNextImageKHR(void* device, void* swapchain,
                                                 uint64_t timeout, void* semaphore,
                                                 void* fence, uint32_t* image_index);
extern "C" uint32_t bionic_vkCreateSwapchainKHR(void* device, const void* create_info,
                                                const void* allocator, void** swapchain);

namespace {
struct VkTapEntry {
    const char* name;
    PFN_vkVoidFunction wrapper;
    void** realSlot;
};
VkTapEntry g_vkTaps[] = {
    {"vkGetPhysicalDeviceSurfaceCapabilitiesKHR",
     reinterpret_cast<PFN_vkVoidFunction>(&bionic_vkSurfaceCaps),
     reinterpret_cast<void**>(&s_realSurfaceCaps)},
    {"vkQueueSubmit", reinterpret_cast<PFN_vkVoidFunction>(&bionic_vkQueueSubmit),
     reinterpret_cast<void**>(&s_realQueueSubmit)},
    {"vkQueuePresentKHR", reinterpret_cast<PFN_vkVoidFunction>(&bionic_vkQueuePresentKHR),
     reinterpret_cast<void**>(&s_realQueuePresent)},
    {"vkAcquireNextImageKHR",
     reinterpret_cast<PFN_vkVoidFunction>(&bionic_vkAcquireNextImageKHR),
     reinterpret_cast<void**>(&s_realAcquireNextImage)},
    {"vkCreateSwapchainKHR",
     reinterpret_cast<PFN_vkVoidFunction>(&bionic_vkCreateSwapchainKHR),
     reinterpret_cast<void**>(&s_realCreateSwapchain)},
};

// Resolve through MoltenVK on demand; null when the game never uses the name.
PFN_vkVoidFunction lookupVkTap(const char* name) {
    if (name == nullptr) return nullptr;
    for (auto& tap : g_vkTaps) {
        if (std::strcmp(name, tap.name) != 0) continue;
        if (*tap.realSlot == nullptr) {
            if (void* mvk = get_mvk_handle()) {
                *tap.realSlot = reinterpret_cast<void*>(::dlsym(mvk, name));
            }
        }
        return *tap.realSlot != nullptr ? tap.wrapper : nullptr;
    }
    return nullptr;
}
}  // namespace
static std::atomic<uint64_t> s_submitCount{0};

// Intercept vkCreateInstance: translate Android surface extension to iOS MoltenVK surface extension
extern "C" VkResult bionic_vkCreateInstance(const VkInstanceCreateInfo* pCreateInfo,
                                            const VkAllocationCallbacks* pAllocator,
                                            VkInstance* pInstance) {
    if (!pCreateInfo || !pInstance) return -3; // VK_ERROR_INITIALIZATION_FAILED
    
    void* mvk = get_mvk_handle();
    PFN_vkCreateInstance real_create = nullptr;
    if (mvk) {
        real_create = (PFN_vkCreateInstance)::dlsym(mvk, "vkCreateInstance");
    }
    if (!real_create) {
        KLOG(kError, "KuDroidGPU", "MoltenVK vkCreateInstance entry point not found in MoltenVK.framework");
        return -3;
    }

    std::vector<const char*> extList;
    if (pCreateInfo->ppEnabledExtensionNames && pCreateInfo->enabledExtensionCount > 0) {
        for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; ++i) {
            const char* ext = pCreateInfo->ppEnabledExtensionNames[i];
            if (strcmp(ext, "VK_KHR_android_surface") == 0) {
                // Translate to MoltenVK iOS surface extensions
                extList.push_back("VK_MVK_ios_surface");
                extList.push_back("VK_EXT_metal_surface");
            } else {
                extList.push_back(ext);
            }
        }
    }

    VkInstanceCreateInfo filteredInfo = *pCreateInfo;
    filteredInfo.enabledExtensionCount = static_cast<uint32_t>(extList.size());
    filteredInfo.ppEnabledExtensionNames = extList.data();

    VkResult res = real_create(&filteredInfo, pAllocator, pInstance);
    if (res == VK_SUCCESS && pInstance) {
        s_activeVkInstance = *pInstance;
    }
    KLOG(kInfo, "KuDroidGPU", "bionic_vkCreateInstance returned %d (instance=%p)", (int)res, (void*)*pInstance);
    return res;
}

#if defined(__APPLE__)
static id kudroid_calayer_self_layer(id self, SEL _cmd) {
    (void)_cmd;
    return self;
}

static void kudroid_ensure_calayer_layer_compat() {
    static bool s_done = false;
    if (!s_done) {
        s_done = true;
        Class clsCALayer = objc_getClass("CALayer");
        if (clsCALayer) {
            SEL selLayer = sel_registerName("layer");
            if (!class_getInstanceMethod(clsCALayer, selLayer)) {
                class_addMethod(clsCALayer, selLayer, (IMP)kudroid_calayer_self_layer, "@@:");
            }
        }
    }
}
#endif

// Translate vkCreateAndroidSurfaceKHR → vkCreateIOSSurfaceMVK / vkCreateMetalSurfaceEXT
extern "C" VkResult bionic_vkCreateAndroidSurfaceKHR(VkInstance instance,
                                                     const VkAndroidSurfaceCreateInfoKHR* pCreateInfo,
                                                     const VkAllocationCallbacks* pAllocator,
                                                     VkSurfaceKHR* pSurface) {
    KLOG(kDebug, "KuDroidGPU", "vkCreateAndroidSurfaceKHR translation");
    if (!pCreateInfo || !pSurface) return -1; // VK_ERROR_INITIALIZATION_FAILED

#if defined(__APPLE__)
    kudroid_ensure_calayer_layer_compat();
#endif

    auto real = get_real_vkGetInstanceProcAddr();
    if (!real) {
        KLOG(kError, "KuDroidGPU", "ERROR: MoltenVK vkGetInstanceProcAddr not found");
        return -1;
    }

    void* actualLayer = g_metalLayer;
    void* windowKey = nullptr;
    std::shared_ptr<SurfaceFrame> windowFrame;
    if (pCreateInfo->window && pCreateInfo->window != (void*)1) {
        auto* nw = static_cast<KuDroidNativeWindow*>(pCreateInfo->window);
        if (nw->magic == 0x4B554457 && nw->layer) {
            actualLayer = nw->layer;
            if (nw->frame) { windowKey = nw->frame.get(); windowFrame = nw->frame; }
        } else {
            actualLayer = pCreateInfo->window;
        }
    }
#if defined(__APPLE__)
    if (!actualLayer) {
        actualLayer = get_or_create_fallback_metal_layer();
    }
    actualLayer = render_layer_for_host(actualLayer, &g_vulkanLayers, "Vulkan", windowKey, windowFrame);
    if (actualLayer == nullptr) return -3;  // VK_ERROR_INITIALIZATION_FAILED

#endif

    KLOG(kDebug, "KuDroidGPU", "Resolved CAMetalLayer pointer for Vulkan Surface: %p", actualLayer);

    // 1. Try the standard function vkCreateMetalSurfaceEXT first (requires CAMetalLayer)
    auto createMetalSurface = (PFN_vkCreateMetalSurfaceEXT)real(instance, "vkCreateMetalSurfaceEXT");
    if (createMetalSurface) {
        VkMetalSurfaceCreateInfoEXT metalInfo = {};
        metalInfo.sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
        metalInfo.pNext = nullptr;
        metalInfo.flags = 0;
        metalInfo.pLayer = actualLayer;

        VkResult r = createMetalSurface(instance, &metalInfo, pAllocator, pSurface);
        KLOG(kInfo, "KuDroidGPU", "vkCreateMetalSurfaceEXT returned %d (surface=%p)", (int)r, (void*)*pSurface);
        // Diagnostic: which layer the surface baked; compare with on-screen at present.
        gpuLog("vkCreateSurface layer=%p -> surface=%p r=%d", actualLayer,
               r == VK_SUCCESS ? (void*)*pSurface : nullptr, (int)r);
        if (r == VK_SUCCESS && *pSurface != nullptr) {
            {
                std::lock_guard<std::mutex> lock(g_surfaceLayersMtx);
                s_vulkanLayer = actualLayer;
                for (auto& e : g_surfaceLayers) {
                    if (e.surface == nullptr) {
                        e.surface = *pSurface;
                        e.layer = actualLayer;
                        break;
                    }
                }
            }
            kudroid_ensure_vulkan_layer_attached(actualLayer);
            return r;
        }
    }

    // 2. Try MoltenVK's iOS function: vkCreateIOSSurfaceMVK
    auto createIOSSurface = (PFN_vkCreateIOSSurfaceMVK)real(instance, "vkCreateIOSSurfaceMVK");
    if (createIOSSurface) {
        VkIOSSurfaceCreateInfoMVK iosInfo = {};
        iosInfo.sType = VK_STRUCTURE_TYPE_IOS_SURFACE_CREATE_INFO_MVK;
        iosInfo.pNext = nullptr;
        iosInfo.flags = 0;
        iosInfo.pView = actualLayer;

        VkResult r = createIOSSurface(instance, &iosInfo, pAllocator, pSurface);
        KLOG(kInfo, "KuDroidGPU", "vkCreateIOSSurfaceMVK returned %d (surface=%p)", (int)r, (void*)*pSurface);
        gpuLog("vkCreateSurface layer=%p -> surface=%p r=%d", actualLayer,
               r == VK_SUCCESS ? (void*)*pSurface : nullptr, (int)r);
        if (r == VK_SUCCESS && *pSurface != nullptr) {
            {
                std::lock_guard<std::mutex> lock(g_surfaceLayersMtx);
                s_vulkanLayer = actualLayer;
                for (auto& e : g_surfaceLayers) {
                    if (e.surface == nullptr) {
                        e.surface = *pSurface;
                        e.layer = actualLayer;
                        break;
                    }
                }
            }
            kudroid_ensure_vulkan_layer_attached(actualLayer);
            return r;
        }
    }

    KLOG(kError, "KuDroidGPU", "ERROR: Neither vkCreateMetalSurfaceEXT nor vkCreateIOSSurfaceMVK succeeded");
    return -1;
}

// Intercept vkGetInstanceProcAddr: translate Android surface calls, forward
// everything else to MoltenVK.
extern "C" PFN_vkVoidFunction bionic_vkGetInstanceProcAddr(VkInstance instance, const char* pName) {
    if (!pName) return nullptr;
    if (strcmp(pName, "vkCreateInstance") == 0) {
        return (PFN_vkVoidFunction)&bionic_vkCreateInstance;
    }
    if (strcmp(pName, "vkCreateAndroidSurfaceKHR") == 0) {
        return (PFN_vkVoidFunction)&bionic_vkCreateAndroidSurfaceKHR;
    }
    if (strcmp(pName, "vkEnumerateInstanceExtensionProperties") == 0) {
        return (PFN_vkVoidFunction)&bionic_vkEnumerateInstanceExtensionProperties;
    }
    if (strcmp(pName, "vkGetDeviceProcAddr") == 0) {
        return (PFN_vkVoidFunction)&bionic_vkGetDeviceProcAddr;
    }
    // Capabilities/swapchain/submit resolve here, never via the device-proc tap.
    if (PFN_vkVoidFunction tap = lookupVkTap(pName)) {
        return tap;
    }
    auto real = get_real_vkGetInstanceProcAddr();
    if (real) {
        return real(instance, pName);
    }
    return nullptr;
}


// Present/acquire tap. Signatures use opaque pointers (no Vulkan headers here);
// only the result code and counters are observed, arguments pass straight through.
static std::atomic<uint64_t> s_presentCount{0};
static std::atomic<uint64_t> s_acquireCount{0};

static void log_present_timing(const void* present_info, uint64_t n) {
    if (!present_info || (n > 3 && n % 120 != 0)) return;
    struct Chain {
        uint32_t type;
        const Chain* next;
    };
    struct PresentTime {
        uint32_t id;
        uint64_t desired;
    };
    struct PresentTimes {
        uint32_t type;
        const Chain* next;
        uint32_t count;
        const PresentTime* times;
    };
    static_assert(sizeof(PresentTime) == 16 && sizeof(PresentTimes) == 32);
    uint64_t desired = 0;
    uint32_t id = 0;
    bool timed = false;
    for (auto* next = static_cast<const Chain*>(present_info)->next; next;
         next = next->next) {
        if (next->type != 1000092000u) continue; // VK_STRUCTURE_TYPE_PRESENT_TIMES_INFO_GOOGLE
        const auto* timing = reinterpret_cast<const PresentTimes*>(next);
        if (!timing->count || !timing->times) return;
        desired = timing->times[0].desired;
        id = timing->times[0].id;
        timed = true;
        break;
    }
    const int64_t now = guest_monotonic_ns();
    const double lead_ms = desired ? (static_cast<double>(desired) - now) / 1.0e6 : 0;
    // Keep timing samples visible after the general GPU log budget is exhausted.
    KLOG(kDebug, "KuDroidGPU",
         "vkTiming #%llu timed=%d id=%u desired_ns=%llu now_ns=%lld lead_ms=%.3f suspend_gap_ms=%.3f",
         static_cast<unsigned long long>(n), timed, id,
         static_cast<unsigned long long>(desired), static_cast<long long>(now),
         lead_ms, static_cast<double>(guest_clock_ns(7) - now) / 1.0e6);
}

extern "C" uint32_t bionic_vkQueuePresentKHR(void* queue, const void* present_info) {
    const uint64_t n = ++s_presentCount;
    log_present_timing(present_info, n);
    // Fail loud, never fake SUCCESS: with no real present behind us the frame
    // went nowhere, and logging SUCCESS for a black screen misdirects every
    // later diagnosis. VK_ERROR_INITIALIZATION_FAILED is a valid VkResult the
    // guest's switch handles; 0xFFFFFFFF was not.
    constexpr uint32_t kVkErrorInit = 0xFFFFFFFDu;  // VK_ERROR_INITIALIZATION_FAILED (-3)
    const uint32_t r =
        s_realQueuePresent != nullptr ? s_realQueuePresent(queue, present_info) : kVkErrorInit;
    if (r == 0 || r == 1000001003u) kudroid_note_frame_presented();
    // Log every present: result + target swapchain/image (standard
    // VkPresentInfoKHR 64-bit layout) so a present stream that stops, fails,
    // or switches swapchain mid-run is visible. ~30 lines/s while running.
    void* swapchain = nullptr;
    uint32_t image_index = 0xFFFFFFFFu;
    if (present_info != nullptr) {
        const void* const* words = static_cast<const void* const*>(present_info);
        // sType(0) pNext(8) waitCount(16) pWait(24) swapCount(32) pSwap(40)
        // pImageIdx(48) pResults(56)
        const void* const* p_swap = static_cast<const void* const*>(words[5]);
        const uint32_t* p_idx = static_cast<const uint32_t*>(words[6]);
        if (p_swap != nullptr) swapchain = const_cast<void*>(p_swap[0]);
        if (p_idx != nullptr) image_index = p_idx[0];
    }
    gpuLog("vkQueuePresentKHR #%llu -> %u swap=%p image=%u submits=%llu",
           (unsigned long long)n, r, swapchain, image_index,
           (unsigned long long)s_submitCount.load(std::memory_order_relaxed));
    if (n == 1) {
        void* target = nullptr;
        {
            std::lock_guard<std::mutex> lock(g_surfaceLayersMtx);
            target = s_vulkanLayer ? s_vulkanLayer : g_metalLayer;
        }
        if (target != nullptr) {
            kudroid_ensure_vulkan_layer_attached(target);
        }
    }
    // Diagnostic: on-screen layer identity; a surface bakes its layer at
    // creation, so a mismatch here means frames go to a hidden layer.
    // 1000001003=SUBOPTIMAL 1000001004=OUT_OF_DATE.
    if (n == 1 || r == 1000001003 || r == 1000001004 || (n % 120) == 0) {
        const char* rname = r == 0 ? "SUCCESS" : (r == 1000001003 ? "SUBOPTIMAL"
                                            : (r == 1000001004 ? "OUT_OF_DATE" : "OTHER"));
        gpuLog("vkPresent #%llu %s onscreen_layer=%p window=%dx%d", (unsigned long long)n,
               rname, g_metalLayer, g_metalLayerWidth, g_metalLayerHeight);
    }
    return r;
}

extern "C" uint32_t bionic_vkAcquireNextImageKHR(void* device, void* swapchain, uint64_t timeout,
                                                void* semaphore, void* fence,
                                                uint32_t* image_index) {    const uint64_t n = ++s_acquireCount;
    uint32_t index = 0xFFFFFFFFu;
    // Valid VkResults only: 0xFFFFFFFF matches no VK_ERROR_* (small negatives)
    // and falls through guest switches into retry-forever. -3 init-failed.
    uint32_t r = 0xFFFFFFFDu;
    if (s_realAcquireNextImage != nullptr) {
        r = s_realAcquireNextImage(device, swapchain, timeout, semaphore, fence, &index);
        if (image_index != nullptr) *image_index = index;
    }
    if (n <= 5 || r != 0 || (n % 120) == 0) {
        gpuLog("vkAcquireNextImageKHR #%llu -> %u image=%u", (unsigned long long)n, r, index);
    }
    return r;
}

// Diagnostic: submit count proves whether Unity draws (submit>0) or only
// clear-presents (submit==0). Command-buffer counts are left to validation
// layers; only the batch count is observed here.
extern "C" uint32_t bionic_vkQueueSubmit(void* queue, uint32_t submit_count,
                                         const void* submits, void* fence) {
    const uint64_t n =
        s_submitCount.fetch_add(submit_count, std::memory_order_relaxed) + submit_count;
    const uint32_t r = s_realQueueSubmit != nullptr
                           ? s_realQueueSubmit(queue, submit_count, submits, fence)
                           : 0xFFFFFFFDu;  // valid VkResult, see above
    if (n <= 5 || r != 0 || (n % 120) == 0) {
        gpuLog("vkQueueSubmit #%llu -> %u batches=%u", (unsigned long long)n, r,
               submit_count);
    }
    return r;
}

extern "C" uint32_t bionic_vkCreateSwapchainKHR(void* device, const void* create_info,
                                               const void* allocator, void** swapchain) {
    // Diagnostic: full swapchain geometry (offsets for 64-bit
    // VkSwapchainCreateInfoKHR). Portrait extent on a landscape game, or
    // preTransform != IDENTITY, is the orientation-split signature.
    void* surface = nullptr;
    uint32_t min_count = 0, format = 0, extent_w = 0, extent_h = 0;
    int32_t pre_transform = -1, present_mode = -1;
    if (create_info != nullptr) {
        const char* ci = static_cast<const char*>(create_info);
        surface = *(void* const*)(ci + 24);
        std::memcpy(&min_count, ci + 32, 4);
        std::memcpy(&format, ci + 36, 4);
        std::memcpy(&extent_w, ci + 44, 4);
        std::memcpy(&extent_h, ci + 48, 4);
        std::memcpy(&pre_transform, ci + 80, 4);
        std::memcpy(&present_mode, ci + 88, 4);
    }
    void* created = nullptr;
    const uint32_t r = s_realCreateSwapchain != nullptr
                           ? s_realCreateSwapchain(device, create_info, allocator, &created)
                           : 0xFFFFFFFDu;  // valid VkResult, see above
    if (swapchain != nullptr) *swapchain = created;
    // The swapchain's layer now owns the pixels: static canvas contents must
    // stop covering its drawables (one producer per layer).
    if (r == 0) {
        void* target = nullptr;
        {
            std::lock_guard<std::mutex> lock(g_surfaceLayersMtx);
            if (surface != nullptr) {
                for (const auto& e : g_surfaceLayers) {
                    if (e.surface == surface && e.layer != nullptr) {
                        s_vulkanLayer = e.layer;
                        break;
                    }
                }
            }
            target = s_vulkanLayer ? s_vulkanLayer : g_metalLayer;
        }
        if (target != nullptr) {
            kudroid_ensure_vulkan_layer_attached(target);
        }
    }
    gpuLog("vkCreateSwapchainKHR surface=%p -> %u swap=%p extent=%ux%u fmt=%u preXform=%d mode=%d minImg=%u onscreen=%p",
           surface, r, created, extent_w, extent_h, format, pre_transform, present_mode,
           min_count, g_metalLayer);
    return r;
}

// Diagnostic: surface capabilities MoltenVK reports. Unity reads these through
// vkGetInstanceProcAddr (bypassing the device-proc tap), so this hook lives on
// the instance-proc path. VkSurfaceCapabilitiesKHR 64-bit layout:
// minCount(0) maxCount(4) currentExtent(8: w,h) supportedTransforms(36)
// currentTransform(40).
extern "C" uint32_t bionic_vkSurfaceCaps(void* phys, void* surface, void* caps) {
    const uint32_t r =
        s_realSurfaceCaps != nullptr ? s_realSurfaceCaps(phys, surface, caps) : 0xFFFFFFFDu;
    if (r == 0 && caps != nullptr) {
        uint32_t w = 0, h = 0, min_c = 0, max_c = 0;
        int32_t sup_xf = -1, cur_xf = -1;
        char* c = static_cast<char*>(caps);
        std::memcpy(&min_c, c + 0, 4);
        std::memcpy(&max_c, c + 4, 4);
        std::memcpy(&w, c + 8, 4);
        std::memcpy(&h, c + 12, 4);
        std::memcpy(&sup_xf, c + 36, 4);
        std::memcpy(&cur_xf, c + 40, 4);
        if (w == 0 || h == 0) {
            w = g_metalLayerWidth > 0 ? static_cast<uint32_t>(g_metalLayerWidth) : 1080u;
            h = g_metalLayerHeight > 0 ? static_cast<uint32_t>(g_metalLayerHeight) : 1920u;
            std::memcpy(c + 8, &w, 4);
            std::memcpy(c + 12, &h, 4);
            uint32_t max_w = 0, max_h = 0;
            std::memcpy(&max_w, c + 24, 4);
            std::memcpy(&max_h, c + 28, 4);
            if (max_w < w) std::memcpy(c + 24, &w, 4);
            if (max_h < h) std::memcpy(c + 28, &h, 4);
            gpuLog("vkSurfaceCaps clamped zero extent to %ux%u", w, h);
        }
        gpuLog("vkSurfaceCaps surface=%p extent=%ux%u curXform=%d supXform=%d min=%u max=%u",
               surface, w, h, cur_xf, sup_xf, min_c, max_c);
    }
    return r;
}

// Run-end reset: present/acquire numbering restarts so the next run's log
// starts at #1 instead of continuing the previous run's sequence.
extern "C" void kudroid_gpu_note_run_end(void) {
    g_materialAuditCount.store(0, std::memory_order_relaxed);
    g_materialAuditEpoch.fetch_add(1, std::memory_order_relaxed);
    s_presentCount.store(0, std::memory_order_relaxed);
    s_acquireCount.store(0, std::memory_order_relaxed);
    std::vector<void*> vkHosts;
    std::vector<void*> glHosts;
    {
        std::lock_guard<std::mutex> lock(g_surfaceLayersMtx);
        s_vulkanLayer = nullptr;
        for (auto& e : g_surfaceLayers) {
            e.surface = nullptr;
            e.layer = nullptr;
        }
        // The run's EGL surfaces are gone; stale entries would keep a layer "owned".
        g_eglLayers.clear();
        g_eglKeys.clear();
        for (const auto& entry : g_glLayers) glHosts.push_back(entry.first);
        for (const auto& entry : g_vulkanLayers) vkHosts.push_back(entry.first);
    }
    for (void* host : glHosts) drop_gl_layer_for_host(host);
#if defined(__APPLE__)
    for (void* host : vkHosts) drop_render_layer_for_host(host, &g_vulkanLayers);
#endif
}

extern "C" PFN_vkVoidFunction bionic_vkGetDeviceProcAddr(VkDevice device, const char* pName) {
    if (!pName) return nullptr;
    // Diagnostic taps; pure passthrough, only for logs.
    if (PFN_vkVoidFunction tap = lookupVkTap(pName)) {
        return tap;
    }
    void* mvk = get_mvk_handle();
    if (mvk) {
        typedef PFN_vkVoidFunction (*PFN_vkGDPA)(VkDevice, const char*);
        auto gdpa = (PFN_vkGDPA)::dlsym(mvk, "vkGetDeviceProcAddr");
        if (gdpa) {
            PFN_vkVoidFunction f = gdpa(device, pName);
            if (f) return f;
        }
    }
    auto real = get_real_vkGetInstanceProcAddr();
    if (real) {
        return real(nullptr, pName);
    }
    return nullptr;
}

// Intercept vkEnumerateInstanceExtensionProperties: inject
// VK_KHR_android_surface and mask VK_EXT_metal_surface so the game sees the
// Android surface extension it expects.
typedef VkResult (*PFN_vkEnumerateInstanceExtensionProperties)(const char*, uint32_t*, void*);

extern "C" VkResult bionic_vkEnumerateInstanceExtensionProperties(const char* pLayerName,
                                                                  uint32_t* pPropertyCount,
                                                                  VkExtensionProperties* pProperties) {
    void* mvk = get_mvk_handle();
    PFN_vkEnumerateInstanceExtensionProperties real = nullptr;
    if (mvk) {
        real = (PFN_vkEnumerateInstanceExtensionProperties)::dlsym(mvk, "vkEnumerateInstanceExtensionProperties");
    }
    if (!real) return -1;

    // First call: get the real count.
    uint32_t realCount = 0;
    VkResult r = real(pLayerName, &realCount, nullptr);
    if (r != VK_SUCCESS) return r;

    // We add VK_KHR_android_surface (and keep VK_EXT_metal_surface masked out).
    const uint32_t added = 1;
    uint32_t total = realCount + added;

    if (!pProperties) {
        *pPropertyCount = total;
        return VK_SUCCESS;
    }

    // Clamp theo capacity.
    const uint32_t capacity = *pPropertyCount;

    // Copy real properties, skipping VK_EXT_metal_surface.
    uint32_t out = 0;
    if (realCount > 0) {
        VkExtensionProperties* tmp = (VkExtensionProperties*)malloc(sizeof(VkExtensionProperties) * realCount);
        if (!tmp) return -1;
        real(pLayerName, &realCount, tmp);

        for (uint32_t i = 0; i < realCount && out < capacity; ++i) {
            if (strcmp(tmp[i].extensionName, "VK_EXT_metal_surface") == 0) {
                continue; // mask it
            }
            memcpy(&pProperties[out], &tmp[i], sizeof(VkExtensionProperties));
            pProperties[out].extensionName[255] = '\0';
            out++;
        }
        free(tmp);
    }

    // Inject VK_KHR_android_surface (only if space is available).
    if (out < capacity) {
        memset(&pProperties[out], 0, sizeof(VkExtensionProperties));
        strncpy(pProperties[out].extensionName, "VK_KHR_android_surface", sizeof(pProperties[out].extensionName) - 1);
        pProperties[out].extensionName[sizeof(pProperties[out].extensionName) - 1] = '\0';
        pProperties[out].specVersion = 1;
        out++;
    }

    *pPropertyCount = out;
    return VK_SUCCESS;
}

} // namespace

void* get_vk_func(const char* name) {
    if (!name) return nullptr;
    if (strcmp(name, "vkGetInstanceProcAddr") == 0) {
        return (void*)&bionic_vkGetInstanceProcAddr;
    }
    if (strcmp(name, "vkGetDeviceProcAddr") == 0) {
        return (void*)&bionic_vkGetDeviceProcAddr;
    }
    if (strcmp(name, "vkCreateInstance") == 0) {
        return (void*)&bionic_vkCreateInstance;
    }
    if (strcmp(name, "vkCreateAndroidSurfaceKHR") == 0) {
        return (void*)&bionic_vkCreateAndroidSurfaceKHR;
    }
    if (strcmp(name, "vkEnumerateInstanceExtensionProperties") == 0) {
        return (void*)&bionic_vkEnumerateInstanceExtensionProperties;
    }
    // Submit/acquire/present/swapchain resolve through raw dlsym as well as the
    // proc-addr hooks; tap them on every path or Unity's route stays invisible.
    // Resolve the real function on demand so this path works even when the game
    // never queries the proc-addr hooks for these names.
    if (strcmp(name, "vkQueueSubmit") == 0 || strcmp(name, "vkQueuePresentKHR") == 0 ||
        strcmp(name, "vkAcquireNextImageKHR") == 0 || strcmp(name, "vkCreateSwapchainKHR") == 0) {
        void* mvk = get_mvk_handle();
        if (strcmp(name, "vkQueueSubmit") == 0) {
            if (s_realQueueSubmit == nullptr && mvk) {
                s_realQueueSubmit = reinterpret_cast<PFN_vkQueueSubmit_fn>(
                    ::dlsym(mvk, "vkQueueSubmit"));
            }
            if (s_realQueueSubmit != nullptr) return (void*)&bionic_vkQueueSubmit;
        } else if (strcmp(name, "vkQueuePresentKHR") == 0) {
            if (s_realQueuePresent == nullptr && mvk) {
                s_realQueuePresent = reinterpret_cast<PFN_vkQueuePresentKHR_fn>(
                    ::dlsym(mvk, "vkQueuePresentKHR"));
            }
            if (s_realQueuePresent != nullptr) return (void*)&bionic_vkQueuePresentKHR;
        } else if (strcmp(name, "vkAcquireNextImageKHR") == 0) {
            if (s_realAcquireNextImage == nullptr && mvk) {
                s_realAcquireNextImage = reinterpret_cast<PFN_vkAcquireNextImageKHR_fn>(
                    ::dlsym(mvk, "vkAcquireNextImageKHR"));
            }
            if (s_realAcquireNextImage != nullptr) {
                return (void*)&bionic_vkAcquireNextImageKHR;
            }
        } else {
            if (s_realCreateSwapchain == nullptr && mvk) {
                s_realCreateSwapchain = reinterpret_cast<PFN_vkCreateSwapchainKHR_fn>(
                    ::dlsym(mvk, "vkCreateSwapchainKHR"));
            }
            if (s_realCreateSwapchain != nullptr) {
                return (void*)&bionic_vkCreateSwapchainKHR;
            }
        }
    }
    void* mvk = get_mvk_handle();
    if (mvk) {
        void* f = ::dlsym(mvk, name);
        if (f) return f;
    }
    auto real = get_real_vkGetInstanceProcAddr();
    if (real) {
        if (s_activeVkInstance) {
            void* f = (void*)real(s_activeVkInstance, name);
            if (f) return f;
        }
        void* f = (void*)real(nullptr, name);
        if (f) return f;
    }
    return ::dlsym(RTLD_DEFAULT, name);
}

