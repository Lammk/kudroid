// AppLifecycle.cpp — install/run lifecycle, JIT status probe, orientation,
// soft-input, permission bridge and the gentle-crash surface the shell polls.
// Split out of kudroid_bridge.cpp.
#include "BridgeState.h"
#include "BridgeShared.h"
#include "ShellUI.h"
#include "kudroid/kudroid_bridge.h"
#include "kudroid/KuArtRuntime.h"
#include "kudroid/VFSPathRemapper.h"
#include "kudroid/PermissionManager.h"
#include "kudroid/APKExtractor.h"
#include "kudroid/ExecMemory.h"
#include "kudroid/Log.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>

extern "C" int kudroid_android_log_message(int priority, const char* tag, const char* message);
void installCrashHandlers(void);
extern "C" void kudroid_log_signal_disposition(const char* tag);
namespace kudroid { int native_frame_in_flight(); }
extern "C" void kuart_send_lifecycle_event(int event);
extern "C" void kudroid_gpu_note_run_end(void);
extern "C" void kudroid_frame_presented_reset(void);

// One in-flight teardown: X, viewWillDisappear and deinit can each post DESTROY.
// File scope so run end can re-arm it; resetting right after unbind allowed a
// second DESTROY into a half-destroyed engine.


extern "C" void kudroid_note_java_paused(void) {
    s_pausedGeneration.fetch_add(1, std::memory_order_relaxed);
}

extern "C" unsigned long long kudroid_paused_generation(void) {
    return s_pausedGeneration.load(std::memory_order_relaxed);
}

// Install generation: bumped on every install/uninstall. Guest .so mappings live
// for the process lifetime (unmapping under a detached thread aborts), so a run
// started after an install would reuse stale code. The run entry refuses instead.

extern "C" const char* kudroid_install_apk(const char* apkPath) {
    std::string log = "[kudroid_core] ===== APK Install =====\n";
    if (!apkPath || !*apkPath) {
        log += "[kudroid_apk] ERROR: APK path is empty\n";
    } else {
        const std::filesystem::path source(apkPath);
        std::string pkgId = kudroid::APKExtractor::get_package_name(source.string());
        if (pkgId.empty()) {
            pkgId = source.stem().string();
            auto uIdx = pkgId.find('_');
            if (uIdx != std::string::npos) {
                pkgId = pkgId.substr(0, uIdx);
            }
        }
        for (char& character : pkgId) {
            if (!(std::isalnum(static_cast<unsigned char>(character)) || character == '_' || character == '-' || character == '.')) {
                character = '_';
            }
        }
        auto& remapper = kudroid::VFSPathRemapper::getInstance();
        const std::filesystem::path appDir = std::filesystem::path(remapper.androidRoot()) /
                                             "data/app" / pkgId;
        log += "[kudroid_apk] APK: " + source.string() + "\n";
        log += "[kudroid_apk] Target Android Package: " + pkgId + "\n";
        log += "[kudroid_apk] Target extraction directory: " + appDir.string() + "\n";
        // Overwrite-only extract leaves files the new APK dropped behind, mixing
        // two versions. Wipe first; skipped while a run holds the files open.
        if (s_isApkRunning.load()) {
            log += "[kudroid_apk] WARNING: app is running, keeping old files until restart\n";
        } else {
            std::error_code wipeEc;
            std::filesystem::remove_all(appDir, wipeEc);
            const auto dalvikRoot = std::filesystem::path(remapper.androidRoot()) /
                                    "data/dalvik-cache";
            std::error_code iterEc;
            if (std::filesystem::is_directory(dalvikRoot, iterEc)) {
                for (const auto& e : std::filesystem::directory_iterator(dalvikRoot, iterEc)) {
                    const std::string name = e.path().filename().string();
                    if (name == pkgId || name.rfind(pkgId + "_", 0) == 0) {
                        std::filesystem::remove_all(e.path(), wipeEc);
                    }
                }
            }
            log += "[kudroid_apk] Wiped previous install state for " + pkgId + "\n";
        }
        ++s_installGeneration;
        bool extractedOk = false;
        if (kudroid::APKExtractor::is_bundle_container(source.string())) {
            log += "[kudroid_apk] Split-APK bundle detected (.xapk/.apks/.apkm), merging splits...\n";
            extractedOk = kudroid::APKExtractor::extract_bundle(source.string(), appDir.string());
        } else {
            extractedOk = kudroid::APKExtractor::extract_apk(source.string(), appDir.string());
        }
        if (extractedOk) {
            // pkgId may come from the APK filename when the manifest is nested; prefer the real package.
            std::string effectiveAppName = pkgId;
            std::filesystem::path effectiveAppDir = appDir;
            {
                std::ifstream infoFile(appDir / "app_info.json");
                std::string line;
                while (std::getline(infoFile, line)) {
                    const auto pos = line.find("\"package\": \"");
                    if (pos == std::string::npos) continue;
                    const auto start = pos + 12;
                    const auto end = line.find('"', start);
                    if (end == std::string::npos) break;
                    const std::string realPkg = line.substr(start, end - start);
                    if (realPkg.empty() || realPkg == pkgId) break;
                    const std::filesystem::path target =
                        std::filesystem::path(remapper.androidRoot()) / "data/app" / realPkg;
                    std::error_code renameEc;
                    std::filesystem::remove_all(target, renameEc);
                    std::filesystem::rename(appDir, target, renameEc);
                    if (!renameEc) {
                        effectiveAppName = realPkg;
                        effectiveAppDir = target;
                        log += "[kudroid_apk] Normalized install dir to package ID: " + realPkg + "\n";
                    }
                    break;
                }
            }

            log += "[kudroid_apk] APK extracted successfully to " + effectiveAppName + "\n";
        } else {
            log += "[kudroid_apk] INSTALL FAILED: " +
                   kudroid::APKExtractor::lastError() + "\n";
        }
    }
    writeLogFile("kudroid_apk_install.txt", log);
    char* result = static_cast<char*>(std::malloc(log.size() + 1));
    if (result) std::memcpy(result, log.c_str(), log.size() + 1);
    return result;
}

#if defined(__APPLE__)
#include <sys/mman.h>
#include <unistd.h>
#include <libkern/OSCacheControl.h>
#include <TargetConditionals.h>

// csops() is a private but stable API for code-signing status.
extern "C" int csops(pid_t pid, unsigned int ops, void* useraddr, size_t usersize);
#ifndef CS_OPS_STATUS
#define CS_OPS_STATUS 0
#endif
#ifndef CS_DEBUGGED
#define CS_DEBUGGED 0x10000000
#endif
#endif

// Return 1 when JIT memory is available, 0 otherwise.
extern "C" int kudroid_is_jit_enabled(void) {
#if defined(__APPLE__)
    // Ask the kernel the way the exec-memory facility does: execute written code
    // (ADRP + RET) under a fault guard. Every route that grants the permission
    // ends in fetchable written memory, and a succeeded mprotect alone no longer
    // proves that — on hardware-W^X regimes it can succeed while the fetch faults.
    //
    // The answer is cached — except when a debugger has attached since the last
    // check. Tools like StikDebug grant JIT permission by attaching after launch,
    // so the first probe legitimately failed; the newly-set CS_DEBUGGED flag is
    // the kernel's own signal that its view changed and the probe must run again.
    const bool txm = kudroid::ExecMemory::TxmPresent();
    const bool fetchable = kudroid::ExecMemory::IsFetchable();
    unsigned int flags = 0;
    const bool csDebugged =
        csops(getpid(), CS_OPS_STATUS, &flags, sizeof(flags)) == 0 &&
        (flags & CS_DEBUGGED) != 0;

    // Diagnostics: one line per verdict change (the UI polls), plus one per
    // re-probe, so a run shows whether the probe failed on a TXM device or the
    // permission simply never arrived.
    static std::atomic<int> s_lastVerdict{-1};
    auto report = [&](int verdict, const char* why) {
        if (s_lastVerdict.exchange(verdict) != verdict) {
            std::fprintf(stderr,
                         "[KuDroidJIT] jit=%d fetchable=%d cs_debugged=%d txm~=%d"
                         " (%s) %s\n",
                         verdict, fetchable ? 1 : 0, csDebugged ? 1 : 0, txm ? 1 : 0,
                         why, kudroid::ExecMemory::RuntimeSummary());
        }
    };

    if (fetchable) {
        report(1, "probe");
        return 1;
    }
    if (!csDebugged) {
        report(0, "no-debugger");
        return 0;
    }

    // A debugger is attached yet exec memory still isn't fetchable (the iOS 26+
    // TXM case: the region was never prepared over the debug connection, so no
    // strategy can pass). Re-probe, but at most once a second — the UI polls and
    // each re-probe runs the whole probe.
    static std::atomic<int64_t> s_lastReprobeMs{0};
    const int64_t nowMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count();
    if (nowMs - s_lastReprobeMs.load(std::memory_order_relaxed) >= 1000) {
        s_lastReprobeMs.store(nowMs, std::memory_order_relaxed);
        const bool ok = kudroid::ExecMemory::Reprobe();
        std::fprintf(stderr, "[KuDroidJIT] reprobe after debugger attach -> %d (txm~=%d)\n",
                     ok ? 1 : 0, txm ? 1 : 0);
        report(ok ? 1 : 0, "reprobe");
        return ok ? 1 : 0;
    }
    report(0, "reprobe-throttled");
    return 0;
#else
    return 1;
#endif
}

int kudroid_jit_available(void) {
    return kudroid_is_jit_enabled();
}

extern "C" const char* kudroid_jit_status(void) {
    // Name the route, not just the verdict.
    const char* text = "JIT: Disabled";
#if defined(__APPLE__)
    if (kudroid_is_jit_enabled()) {
        static char buf[64];
        std::snprintf(buf, sizeof(buf), "JIT: Enabled (%s)", kudroid::ExecMemory::ModeName());
        text = buf;
    }
#else
    if (kudroid_is_jit_enabled()) text = "JIT: Enabled";
#endif
    const size_t length = strlen(text) + 1;
    char* result = (char*)malloc(length);
    if (result) memcpy(result, text, length);
    return result;
}


#if defined(__APPLE__)
extern "C" __attribute__((weak)) void kudroid_notify_orientation_change(int orientation) { (void)orientation; }
#else
extern "C" void kudroid_notify_orientation_change(int orientation) { (void)orientation; }
#endif

extern "C" void kudroid_set_requested_orientation(int orientation) {
    g_requestedOrientation.store(orientation);
    kudroid_notify_orientation_change(orientation);
    fprintf(stderr, "[KuDroidCore] Screen orientation requested: %d (%s)\n",
            orientation,
            (orientation == 0 || orientation == 6 || orientation == 8) ? "Landscape" :
            (orientation == 1 || orientation == 7 || orientation == 9) ? "Portrait" : "Sensor/Auto");
}

extern "C" int kudroid_get_requested_orientation(void) {
    return g_requestedOrientation.load();
}

extern "C" JNIEXPORT void JNICALL Java_android_app_Activity_setRequestedOrientation_1native(JNIEnv* env, jclass clazz, jint orientation) {
    (void)env; (void)clazz;
    kudroid_set_requested_orientation(orientation);
}

extern "C" void kudroid_blit_canvas_to_layer(void* layer, const void* bits, int width, int height);
extern "C" bool kudroid_gpu_has_active_surface(void);

extern "C" void kudroid_unbind_metal_layer(void) {
    g_metalLayer = nullptr;
    g_metalLayerWidth = 0;
    g_metalLayerHeight = 0;
    s_isApkRunning.store(false);
    kudroid_android_log_message(2, "KuDroidGPU", "kudroid_unbind_metal_layer: GPU surface unbound cleanly.");
}

static uint32_t* s_softwareFrameBuffer = nullptr;
static size_t s_softwareFrameBufferSize = 0;
static int s_softwareWidth = 1080;
static int s_softwareHeight = 1920;
static std::mutex s_canvasMutex;

static void ensure_software_framebuffer() {
    int w = g_metalLayerWidth > 0 ? g_metalLayerWidth : 1080;
    int h = g_metalLayerHeight > 0 ? g_metalLayerHeight : 1920;
    s_softwareWidth = w;
    s_softwareHeight = h;
    size_t needed = (size_t)w * h * sizeof(uint32_t);
    if (!s_softwareFrameBuffer || needed > s_softwareFrameBufferSize) {
        void* nb = std::realloc(s_softwareFrameBuffer, needed);
        if (nb) {
            s_softwareFrameBuffer = static_cast<uint32_t*>(nb);
            s_softwareFrameBufferSize = needed;
            std::memset(s_softwareFrameBuffer, 0, needed);
        }
    }
}

// Chuyn ARGB (Android format) sang RGBA / BGRA (iOS Metal format)
static inline uint32_t argb_to_rgba(uint32_t argb) {
    uint32_t a = (argb >> 24) & 0xFF;
    uint32_t r = (argb >> 16) & 0xFF;
    uint32_t g = (argb >> 8) & 0xFF;
    uint32_t b = (argb) & 0xFF;
    return (r) | (g << 8) | (b << 16) | (a << 24);
}

extern "C" JNIEXPORT void JNICALL Java_android_graphics_Canvas_native_1drawColor(JNIEnv* env, jclass clazz, jint color) {
    (void)env; (void)clazz;
    std::lock_guard<std::mutex> lock(s_canvasMutex);
    ensure_software_framebuffer();
    if (!s_softwareFrameBuffer) return;
    uint32_t rgba = argb_to_rgba(static_cast<uint32_t>(color));
    size_t total = (size_t)s_softwareWidth * s_softwareHeight;
    for (size_t i = 0; i < total; ++i) {
        s_softwareFrameBuffer[i] = rgba;
    }
}

extern "C" JNIEXPORT void JNICALL Java_android_graphics_Canvas_native_1drawRect(JNIEnv* env, jclass clazz, jfloat left, jfloat top, jfloat right, jfloat bottom, jint color) {
    (void)env; (void)clazz;
    std::lock_guard<std::mutex> lock(s_canvasMutex);
    ensure_software_framebuffer();
    if (!s_softwareFrameBuffer) return;

    int x0 = std::max(0, std::min(s_softwareWidth - 1, static_cast<int>(left)));
    int y0 = std::max(0, std::min(s_softwareHeight - 1, static_cast<int>(top)));
    int x1 = std::max(0, std::min(s_softwareWidth - 1, static_cast<int>(right)));
    int y1 = std::max(0, std::min(s_softwareHeight - 1, static_cast<int>(bottom)));
    uint32_t rgba = argb_to_rgba(static_cast<uint32_t>(color));

    for (int y = y0; y <= y1; ++y) {
        uint32_t* row = s_softwareFrameBuffer + y * s_softwareWidth;
        for (int x = x0; x <= x1; ++x) {
            row[x] = rgba;
        }
    }
}

extern "C" JNIEXPORT void JNICALL Java_android_graphics_Canvas_native_1drawText(JNIEnv* env, jclass clazz, jstring text, jfloat x, jfloat y, jint color, jfloat textSize) {
    (void)env; (void)clazz; (void)text; (void)textSize;
    Java_android_graphics_Canvas_native_1drawRect(env, clazz, x, y - 16.0f, x + 250.0f, y + 4.0f, color);
}

extern "C" JNIEXPORT void JNICALL Java_android_graphics_Canvas_native_1drawBitmap(JNIEnv* env, jclass clazz, jintArray pixels, jint width, jint height, jfloat x, jfloat y) {
    (void)env; (void)clazz;
    if (!pixels || width <= 0 || height <= 0) return;
    std::lock_guard<std::mutex> lock(s_canvasMutex);
    ensure_software_framebuffer();
    if (!s_softwareFrameBuffer) return;

    jint* src = env->GetIntArrayElements(pixels, nullptr);
    if (!src) return;

    int dstX = static_cast<int>(x);
    int dstY = static_cast<int>(y);

    for (int r = 0; r < height; ++r) {
        int py = dstY + r;
        if (py < 0 || py >= s_softwareHeight) continue;
        for (int c = 0; c < width; ++c) {
            int px = dstX + c;
            if (px < 0 || px >= s_softwareWidth) continue;
            uint32_t pixel = static_cast<uint32_t>(src[r * width + c]);
            s_softwareFrameBuffer[py * s_softwareWidth + px] = argb_to_rgba(pixel);
        }
    }

    env->ReleaseIntArrayElements(pixels, src, JNI_ABORT);
}

extern "C" JNIEXPORT void JNICALL Java_android_graphics_Canvas_native_1flush(JNIEnv* env, jclass clazz) {
    (void)env; (void)clazz;
    if (kudroid_gpu_has_active_surface()) return;
    std::lock_guard<std::mutex> lock(s_canvasMutex);
    if (g_metalLayer && s_softwareFrameBuffer && s_softwareWidth > 0 && s_softwareHeight > 0) {
        kudroid_blit_canvas_to_layer(g_metalLayer, s_softwareFrameBuffer, s_softwareWidth, s_softwareHeight);
    }
}

#if defined(__APPLE__)
extern "C" __attribute__((weak)) void kudroid_trigger_haptic(int intensity) { (void)intensity; }
#else
extern "C" void kudroid_trigger_haptic(int intensity) { (void)intensity; }
#endif

extern "C" void kudroid_vibrate(int intensity) {
    kudroid_trigger_haptic(intensity);
    fprintf(stderr, "[KuDroidCore] kudroid_vibrate(intensity=%d: %s)\n",
            intensity,
            intensity == 1 ? "Light" : (intensity == 2 ? "Medium" : "Heavy"));
}

extern "C" JNIEXPORT void JNICALL Java_android_os_Vibrator_kudroid_1vibrate_1native(JNIEnv* env, jclass clazz, jint intensity) {
    (void)env; (void)clazz;
    kudroid_vibrate(intensity);
}


extern "C" void kudroid_set_keep_screen_on(int keepOn) {
    s_keepScreenOn.store(keepOn != 0 ? 1 : 0);
    fprintf(stderr, "[KuDroidCore] kudroid_set_keep_screen_on(%d)\n", keepOn != 0 ? 1 : 0);
}

extern "C" int kudroid_get_keep_screen_on(void) {
    return s_keepScreenOn.load();
}

extern "C" JNIEXPORT void JNICALL Java_android_view_Window_setKeepScreenOnNative(JNIEnv* env, jclass clazz, jboolean keepOn) {
    (void)env; (void)clazz;
    kudroid_set_keep_screen_on(keepOn ? 1 : 0);
}

extern "C" JNIEXPORT void JNICALL Java_android_view_View_setKeepScreenOnNative(JNIEnv* env, jclass clazz, jboolean keepOn) {
    (void)env; (void)clazz;
    kudroid_set_keep_screen_on(keepOn ? 1 : 0);
}

extern "C" JNIEXPORT void JNICALL Java_android_os_PowerManager_00024WakeLock_setKeepScreenOnNative(JNIEnv* env, jclass clazz, jboolean keepOn) {
    (void)env; (void)clazz;
    kudroid_set_keep_screen_on(keepOn ? 1 : 0);
}

// ── soft keyboard ────────────────────────────────────────────────────────────
//
// KuDroid has no keyboard of its own; iOS does. The guest's
// InputMethodManager.showSoftInput reaches kudroid_show_soft_input, which forwards
// to a callback the Swift side registers — kudroid_core is a static library and
// cannot touch UIKit, so the view that becomes first responder has to call back in.
//
// Text goes the other way, from the host keyboard into the guest's focused
// InputConnection, through kuart_dispatch_text_input.

static kudroid_soft_input_show_cb s_softInputShow = nullptr;
static kudroid_soft_input_hide_cb s_softInputHide = nullptr;

extern "C" void kudroid_set_soft_input_callbacks(kudroid_soft_input_show_cb show,
                                                kudroid_soft_input_hide_cb hide) {
    s_softInputShow = show;
    s_softInputHide = hide;
    fprintf(stderr, "[KuDroidCore] soft input callbacks %s\n",
            (show != nullptr || hide != nullptr) ? "registered" : "cleared");
}

extern "C" int kudroid_show_soft_input(int flags) {
    fprintf(stderr, "[KuDroidCore] kudroid_show_soft_input(flags=0x%x) host=%s\n",
            flags, s_softInputShow != nullptr ? "yes" : "none");
    if (s_softInputShow == nullptr) return 0;
    s_softInputShow(flags);
    return 1;
}

extern "C" int kudroid_hide_soft_input(void) {
    fprintf(stderr, "[KuDroidCore] kudroid_hide_soft_input() host=%s\n",
            s_softInputHide != nullptr ? "yes" : "none");
    if (s_softInputHide == nullptr) return 0;
    s_softInputHide();
    return 1;
}

extern "C" int kudroid_is_soft_input_visible(void) {
    return s_softInputVisible.load();
}

extern "C" void kudroid_set_soft_input_visible(int visible) {
    const int v = visible != 0 ? 1 : 0;
    if (s_softInputVisible.exchange(v) != v) {
        fprintf(stderr, "[KuDroidCore] soft input now %s\n", v ? "visible" : "hidden");
    }
}

extern "C" void kudroid_dispatch_text_input(const char* utf8) {
    if (utf8 == nullptr || utf8[0] == '\0') return;
    kuart_dispatch_text_input(utf8);
}

extern "C" void kudroid_dispatch_delete_backward(void) {
    kuart_dispatch_delete_backward();
}

// Gentle Crash API
// ─────────────────────────────────────────────────────────────────────────────
extern "C" int kudroid_has_crashed(void) {
    return g_hasCrashed.load() ? 1 : 0;
}

extern "C" void kudroid_clear_crash_state(void) {
    g_hasCrashed.store(false);
    g_lastCrashTail[0] = '\0';
}

extern "C" void kudroid_stop_app(void) {
    // Teardown that does not run bytecode happens on the caller's thread, so the
    // shell sees the app marked stopped the moment ✕ is pressed.
    s_isApkRunning.store(false);
    kudroid_clear_crash_state();
    kudroid_set_requested_orientation(1); // SCREEN_ORIENTATION_PORTRAIT

    // DESTROY_ACTIVITY runs Java, and Interpreter::Execute takes the global VM lock.
    // This is called from the iOS main thread, so doing it here froze the whole UI
    // whenever a guest thread held that lock — which is exactly the situation when a
    // guest is wedged, i.e. every time a user actually reaches for ✕. The screen
    // stopped responding and the app had to be killed from the multitasking switcher.
    //
    // Detached rather than joined: the point is that the caller must not wait. If the
    // guest is unrecoverably stuck the event may never be delivered, and that is
    // still better than taking the UI down with it.
    //
    // Guarded to one in-flight teardown: X, viewWillDisappear and deinit can each
    // post DESTROY, and a second post races the first's onDestroy->nativeDone,
    // destroying an engine that is already half destroyed.
    bool expected = false;
    if (!s_stopping.compare_exchange_strong(expected, true)) return;
    kudroid_log_signal_disposition("stop-app");
    std::thread([] {
        // PAUSE first: the player loop must stop rendering before DESTROY runs
        // nativeDone, or teardown races in-flight frames on a torn surface.
        const unsigned long long pausedBefore = kudroid_paused_generation();
        kuart_send_lifecycle_event(101);  // PAUSE_ACTIVITY
        // Up to 2s for the Java pause handler to run (Unity's own pause timeout).
        for (int i = 0; i < 400 && kudroid_paused_generation() == pausedBefore; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        // Drain frames already inside nativeRender (2s cap matches Unity's own
        // pause timeout); unbind stays at run end.
        for (int i = 0; i < 400 && kudroid::native_frame_in_flight() > 0; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        kuart_send_lifecycle_event(103);  // DESTROY_ACTIVITY
    }).detach();
}

extern "C" const char* kudroid_get_last_crash_tail(void) {
    if (g_lastCrashTail[0] == '\0') {
        return strdup("No recent crash detected.");
    }
    return strdup(g_lastCrashTail);
}

// Permission Dialog Prompt Bridge
// ─────────────────────────────────────────────────────────────────────────────
static kudroid_permission_prompt_cb g_permission_prompt_cb = nullptr;

extern "C" void kudroid_set_permission_prompt_callback(kudroid_permission_prompt_cb cb) {
    g_permission_prompt_cb = cb;
}

extern "C" void kudroid_prompt_permission_request(const char* packageName, const char* permissionsCsv, int requestCode, void* activityHandle) {
    if (g_permission_prompt_cb != nullptr) {
        g_permission_prompt_cb(packageName, permissionsCsv, requestCode, activityHandle);
    } else {
        kudroid_submit_permission_response(activityHandle, requestCode, permissionsCsv, 1);
    }
}

extern "C" void kudroid_submit_permission_response(void* activityHandle, int requestCode, const char* permissionsCsv, int granted) {
    if (activityHandle == nullptr || permissionsCsv == nullptr) return;
    std::string csv = permissionsCsv;
    std::vector<std::string> perms;
    std::string token;
    std::istringstream tokenStream(csv);
    while (std::getline(tokenStream, token, ',')) {
        if (!token.empty()) perms.push_back(token);
    }
    if (perms.empty()) return;

    // Log permission grant result
    std::fprintf(stderr, "[PermissionManager] Permission response for request %d (%zu perms): %s\n",
                 requestCode, perms.size(), granted ? "GRANTED" : "DENIED");
}
