// test_metal_layer_rebind.cpp — a guest window must follow the CAMetalLayer the
// shell is actually presenting.
//
// Reproduces the ULTRAKILL black screen exactly as the device logs recorded it:
//
//   kudroid_set_metal_layer(layer=0x10f554e40, size=828x1792)    <- app-wide view
//   ANativeWindow_fromSurface allocated ... (layer=0x10f554e40)  <- Unity takes it
//   kudroid_set_metal_layer(layer=0x10424fb10, size=1792x828)    <- runner's view
//
// Unity calls ANativeWindow_fromSurface once, from nativeRecreateGfxState, and keeps
// the pointer for the life of the process. The second layer arrives afterwards. So
// the guest went on rendering into 0x10f554e40 while the screen showed 0x10424fb10:
// a black screen with the guest drawing correctly the whole time, and nothing in any
// log anywhere near the cause.
//
// The shim is driven directly rather than through a JNIEnv: with a null env,
// ANativeWindow_fromSurface returns its fallback window, which is the same struct on
// the same registry and is what the rebind has to reach.
#include <cstdio>
#include <string>

extern "C" {
// GraphicsShim.cpp
void* bionic_ANativeWindow_fromSurface(void* env, void* surface);
void bionic_ANativeWindow_release(void* window);
void bionic_ANativeWindow_acquire(void* window);
int bionic_ANativeWindow_getWidth(void* window);
int bionic_ANativeWindow_getHeight(void* window);
int bionic_ANativeWindow_setBuffersGeometry(void* window, int w, int h, int format);
void kudroid_gpu_rebind_native_windows(void* layer, int width, int height);
void* kudroid_gpu_native_window_layer(void* window);

// kudroid_bridge.cpp
void kudroid_set_metal_layer(void* layer, int width, int height, float density);
void kudroid_unbind_metal_layer(void);
}

namespace {

int g_failures = 0;

void Check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "  OK  " : "  FAIL", what.c_str());
    if (!ok) ++g_failures;
}

// Stand-ins for two CAMetalLayers. Only their identity matters here; nothing
// dereferences them on a Linux host.
int g_layerA = 0;
int g_layerB = 0;
void* const kLayerA = &g_layerA;
void* const kLayerB = &g_layerB;

// The device sequence, verbatim.
void test_guest_window_follows_second_layer() {
    std::printf("[rebind] a window taken before the second bind still follows it\n");

    kudroid_set_metal_layer(kLayerA, 828, 1792, 2.0f);
    void* window = bionic_ANativeWindow_fromSurface(nullptr, nullptr);
    Check(window != nullptr, "the guest gets a window");
    Check(kudroid_gpu_native_window_layer(window) == kLayerA,
          "it starts on the layer that was bound at the time");
    Check(bionic_ANativeWindow_getWidth(window) == 828, "and that layer's width");
    Check(bionic_ANativeWindow_getHeight(window) == 1792, "and that layer's height");

    // The runner installs its own view. The guest never calls fromSurface again.
    kudroid_set_metal_layer(kLayerB, 1792, 828, 2.0f);
    Check(kudroid_gpu_native_window_layer(window) == kLayerB,
          "the window the guest already holds now points at the presented layer — the "
          "black screen was this staying on the old one");
    Check(bionic_ANativeWindow_getWidth(window) == 1792, "width follows the new surface");
    Check(bionic_ANativeWindow_getHeight(window) == 828, "height follows the new surface");

    bionic_ANativeWindow_release(window);
}

// Re-binding the same layer must not churn: the shell calls set_metal_layer from
// layoutSubviews, which fires on every rotation and resize.
void test_same_layer_is_not_disturbed() {
    std::printf("[rebind] re-binding the identical layer changes nothing\n");

    kudroid_set_metal_layer(kLayerA, 828, 1792, 2.0f);
    void* window = bionic_ANativeWindow_fromSurface(nullptr, nullptr);
    bionic_ANativeWindow_setBuffersGeometry(window, 640, 480, 1);
    Check(bionic_ANativeWindow_getWidth(window) == 640,
          "the guest's own choice of buffer size is in effect");

    kudroid_set_metal_layer(kLayerA, 828, 1792, 2.0f);
    Check(kudroid_gpu_native_window_layer(window) == kLayerA, "still the same layer");
    Check(bionic_ANativeWindow_getWidth(window) == 640,
          "and the guest's chosen size survives — a guest has already sized its "
          "swapchain to it");
    Check(bionic_ANativeWindow_getHeight(window) == 480, "both dimensions survive");

    bionic_ANativeWindow_release(window);
}

// A null layer means "nothing to present" (kudroid_unbind_metal_layer on exit).
// Rebinding to null would hand the guest a null layer mid-frame.
void test_unbind_does_not_null_live_windows() {
    std::printf("[rebind] unbinding does not null out a live window's layer\n");

    kudroid_set_metal_layer(kLayerA, 828, 1792, 2.0f);
    void* window = bionic_ANativeWindow_fromSurface(nullptr, nullptr);
    Check(kudroid_gpu_native_window_layer(window) == kLayerA, "window is on layer A");

    kudroid_unbind_metal_layer();
    Check(kudroid_gpu_native_window_layer(window) == kLayerA,
          "the window keeps its layer rather than being handed nullptr mid-frame");

    bionic_ANativeWindow_release(window);
}

// The registry must not outlive the windows in it, or a later set_metal_layer
// writes through a dangling pointer. Heap windows only exist on the JNIEnv path,
// so this drives the release/acquire refcount directly.
void test_release_refcount_is_symmetric() {
    std::printf("[rebind] release decrements exactly once per call\n");

    kudroid_set_metal_layer(kLayerA, 828, 1792, 2.0f);
    void* window = bionic_ANativeWindow_fromSurface(nullptr, nullptr);

    // acquire/release round trips must balance. The old code decremented twice on
    // any release that was not the final one, so a window could be freed while the
    // guest still held it — and the registry would then be pointing at freed memory.
    for (int i = 0; i < 4; ++i) bionic_ANativeWindow_acquire(window);
    for (int i = 0; i < 4; ++i) bionic_ANativeWindow_release(window);

    Check(kudroid_gpu_native_window_layer(window) == kLayerA,
          "the window is still alive and still bound after balanced acquire/release");

    kudroid_set_metal_layer(kLayerB, 1792, 828, 2.0f);
    Check(kudroid_gpu_native_window_layer(window) == kLayerB,
          "and it is still reachable from the registry afterwards");

    bionic_ANativeWindow_release(window);
}

// Called with no window bound at all: must not crash and must not invent one.
void test_rebind_with_no_windows() {
    std::printf("[rebind] rebinding with nothing registered is a no-op\n");
    kudroid_gpu_rebind_native_windows(kLayerB, 1792, 828);
    Check(kudroid_gpu_native_window_layer(nullptr) == nullptr,
          "a null window has no layer");
    Check(true, "rebinding with no live windows does not crash");
}

}  // namespace

int main() {
    std::printf("=== metal layer rebind ===\n");
    test_guest_window_follows_second_layer();
    test_same_layer_is_not_disturbed();
    test_unbind_does_not_null_live_windows();
    test_release_refcount_is_symmetric();
    test_rebind_with_no_windows();

    if (g_failures == 0) {
        std::printf("=== all metal layer rebind checks passed ===\n");
        return 0;
    }
    std::printf("=== %d FAILURE(S) ===\n", g_failures);
    return 1;
}
