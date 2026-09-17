// ShellUI.cpp — Metal layer ownership, the software-canvas Canvas natives,
// haptics, keep-screen-on and the VFS test-log entry points. Split out of
// kudroid_bridge.cpp.
#include "BridgeState.h"
#include "ShellUI.h"
#include "kudroid/platform/JavaCanvasRenderer.h"
#include "kudroid/VFSPathRemapper.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>

extern "C" int kudroid_android_log_message(int priority, const char* tag, const char* message);
extern "C" void* bionic_ANativeWindow_fromSurface(void* env, void* surface);
extern "C" void kudroid_gpu_warmup_egl(void);
extern "C" void kudroid_gpu_rebind_native_windows(void* layer, int width, int height);
extern "C" void kudroid_gpu_resize_native_windows(void* layer, int oldW, int oldH,
                                                  int width, int height);

// pointer lp metal ton cc c th truy cp bng bionicshim
void* g_metalLayer = nullptr;
int g_metalLayerWidth = 1080;
int g_metalLayerHeight = 1920;
float g_metalLayerDensity = 3.0f;

// Via the standard log pipeline; defined in SyscallShim.cpp, shared with GraphicsShim.
extern "C" int kudroid_android_log_message(int priority, const char* tag, const char* message);

// ANGLE first-touch must run on the main thread.
extern "C" void kudroid_gpu_warmup_egl(void);
extern "C" void* bionic_ANativeWindow_fromSurface(void* env, void* surface);
extern "C" void kudroid_gpu_rebind_native_windows(void* layer, int width, int height);
extern "C" void kudroid_gpu_resize_native_windows(void* layer, int oldW, int oldH,
                                                  int width, int height);

extern "C" void kudroid_set_metal_layer(void* layer, int width, int height, float density) {
    void* const previousLayer = g_metalLayer;
    const int previousWidth = g_metalLayerWidth;
    const int previousHeight = g_metalLayerHeight;
    g_metalLayer = layer;
    g_metalLayerWidth = width;
    g_metalLayerHeight = height;
    g_metalLayerDensity = density > 0.0f ? density : g_metalLayerDensity;
    // Log the size from Swift; wrong sizes otherwise surface only at lock time.
    char buf[192];
    std::snprintf(buf, sizeof(buf),
                  "kudroid_set_metal_layer(layer=%p, size=%dx%d, density=%.2f)",
                  layer, width, height, density);
    kudroid_android_log_message(2, "KuDroidGPU", buf);

    // Resize the software canvas to the real surface. The Java-side Canvas asks for
    // these numbers via native_getSurfaceWidth/Height, so layout is computed for the
    // actual screen instead of the old hardcoded 1080x1920.
    if (width > 0 && height > 0) {
        kudroid::JavaCanvasRenderer::getInstance().init(width, height);
    }

    // Move any window the guest already holds onto this layer.
    //
    // The shell binds twice: once for the app-wide view at startup, then again when
    // the guest-app runner installs its own on top. A guest that took its
    // ANativeWindow between those two calls — Unity does, from
    // nativeRecreateGfxState — would otherwise keep rendering into the first layer
    // for the rest of the process, which by then is behind the new one. The result
    // is a black screen with the guest rendering correctly the whole time and
    // nothing in any log pointing here.
    if (layer != nullptr && layer != previousLayer) {
        kudroid_gpu_rebind_native_windows(layer, width, height);
    }
    // Same layer, new size (rotation): windows still following the layer adopt
    // it. A guest that called setBuffersGeometry chose its own resolution and
    // keeps it — detected by size differing from what the layer had.
    if (layer != nullptr && layer == previousLayer && width > 0 && height > 0 &&
        (width != previousWidth || height != previousHeight)) {
        kudroid_gpu_resize_native_windows(layer, previousWidth, previousHeight,
                                          width, height);
    }

    // EGL warmup runs after the display is created.
    // kudroid_gpu_warmup_egl();
}

extern "C" const char* kudroid_vfs_self_test_log(void) {
    const std::string log = kudroid::run_vfs_self_test();
    writeLogFile("kudroid_vfs_selftest.txt", log);
    char* result = static_cast<char*>(std::malloc(log.size() + 1));
    if (result) std::memcpy(result, log.c_str(), log.size() + 1);
    return result;
}

extern "C" const char* kudroid_vfs_extended_test_log(void) {
    const std::string log = kudroid::run_vfs_extended_test();
    writeLogFile("kudroid_vfs_extended_test.txt", log);
    char* result = static_cast<char*>(std::malloc(log.size() + 1));
    if (result) std::memcpy(result, log.c_str(), log.size() + 1);
    return result;
}
