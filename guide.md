# Graphics Shim API Translation Guide
*For DeepSeek: Implement these mappings in `src/shims/GraphicsShim.cpp`*

## 1. Vulkan ↔ MoltenVK Translation
Android games use the Android-specific Vulkan surface extension. On iOS, MoltenVK uses Apple's Metal surface extension. The GraphicsShim must intercept the instance creation and surface creation to bridge this gap.

### Intercepting `vkGetInstanceProcAddr`
You must hook `vkGetInstanceProcAddr` to intercept the resolution of Vulkan function pointers.
```cpp
PFN_vkVoidFunction vkGetInstanceProcAddr_shim(VkInstance instance, const char* pName) {
    if (strcmp(pName, "vkCreateAndroidSurfaceKHR") == 0) {
        return (PFN_vkVoidFunction)vkCreateAndroidSurfaceKHR_shim;
    }
    // Forward all others to MoltenVK's real vkGetInstanceProcAddr
    return real_vkGetInstanceProcAddr(instance, pName);
}
```

### Translating Surface Creation
When the game calls `vkCreateAndroidSurfaceKHR`, translate it to `vkCreateMetalSurfaceEXT`.
```cpp
VkResult vkCreateAndroidSurfaceKHR_shim(VkInstance instance, 
                                        const VkAndroidSurfaceCreateInfoKHR* pCreateInfo, 
                                        const VkAllocationCallbacks* pAllocator, 
                                        VkSurfaceKHR* pSurface) {
    // 1. Extract CAMetalLayer from pCreateInfo->window (which is our fake ANativeWindow)
    CAMetalLayer* layer = get_metal_layer_from_native_window(pCreateInfo->window);

    // 2. Map to VkMetalSurfaceCreateInfoEXT
    VkMetalSurfaceCreateInfoEXT metalCreateInfo = {};
    metalCreateInfo.sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
    metalCreateInfo.pNext = nullptr;
    metalCreateInfo.flags = 0;
    metalCreateInfo.pLayer = layer;

    // 3. Call MoltenVK's actual function
    auto createMetalSurface = (PFN_vkCreateMetalSurfaceEXT)real_vkGetInstanceProcAddr(instance, "vkCreateMetalSurfaceEXT");
    return createMetalSurface(instance, &metalCreateInfo, pAllocator, pSurface);
}
```

### Required Vulkan Exports (from libvulkan.so shim)
Expose these symbols in your dlsym resolver for `libvulkan.so`:
- `vkGetInstanceProcAddr` (shimmed as above)
- `vkGetDeviceProcAddr` (can usually pass through to MoltenVK directly)
- `vkEnumerateInstanceExtensionProperties` (must inject `VK_KHR_android_surface` into the returned list, masking `VK_EXT_metal_surface`)

---

## 2. OpenGL ES (EGL) ↔ ANGLE Translation
ANGLE implements EGL and GLES on top of Metal for iOS. The shim is simpler because ANGLE's EGL implementation is cross-platform.

### Translating EGL Window Surface
When the game creates an EGL surface, it passes an `ANativeWindow`. ANGLE on iOS expects a `CAMetalLayer` or `UIView`.
```cpp
EGLSurface eglCreateWindowSurface_shim(EGLDisplay dpy, EGLConfig config,
                                       EGLNativeWindowType win,
                                       const EGLint *attrib_list) {
    // Extract CAMetalLayer from the fake ANativeWindow
    CAMetalLayer* layer = get_metal_layer_from_native_window(win);
    
    // Pass the CAMetalLayer directly to ANGLE's eglCreateWindowSurface
    return real_eglCreateWindowSurface(dpy, config, (EGLNativeWindowType)layer, attrib_list);
}
```

### Required EGL Exports (from libEGL.so shim)
Expose these symbols in your dlsym resolver for `libEGL.so`:
- `eglGetProcAddress` (intercept to return shims if necessary, otherwise forward to ANGLE)
- `eglCreateWindowSurface` (shimmed as above)
- All other standard EGL calls (`eglInitialize`, `eglChooseConfig`, `eglMakeCurrent`, `eglSwapBuffers`) can be passed directly to ANGLE.

### Required GLES Exports (from libGLESv2.so / libGLESv3.so shim)
Standard GLES functions (e.g., `glDrawArrays`, `glClear`) can be directly forwarded to ANGLE's GLES library. You can resolve them in batch using ANGLE's dylib, or let the game resolve them via the shimmed `eglGetProcAddress`.

---

## 3. The Fake `ANativeWindow`
Both Vulkan and OpenGL shims rely on extracting the `CAMetalLayer` from the `ANativeWindow` passed by the game. You must define a struct that exactly matches Android's `ANativeWindow` ABI, but holds a pointer to your `CAMetalLayer` inside it.

```cpp
struct ANativeWindow_Impl {
    // Standard Android ANativeWindow fields...
    void* reserved; 
    // ...
    // KuDroid custom field:
    void* metal_layer; 
};
```
