// GuestLibrary.cpp — JVM self-test, native-library discovery and the
// dlopen/dlsym/dlclose shims for guest .so files. Split out of kudroid_bridge.cpp.
#include "BridgeState.h"
#include "BridgeShared.h"
#include "GuestLibrary.h"
#include "CrashInternal.h"
#include "kudroid/KuArtRuntime.h"
#include "kudroid/elf_loader.hpp"
#include "kudroid/BionicShim.h"
#include "kudroid/NativeCallTelemetry.h"
#include "kudroid/abi/BlockingWaitRegistry.h"
#include "kudroid/kudroid_bridge.h"
#include "kudroid/VFSPathRemapper.h"
#include "kudroid/APKExtractor.h"
#include "kudroid/DeviceProfile.h"
#include "kudroid/platform/AssetShim.h"
#include "kudroid/platform/InputShim.h"
#include "kudroid/Log.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <mutex>
#include <set>
#include <string>
#include <filesystem>
#include <fstream>

extern "C" int kudroid_android_log_message(int priority, const char* tag, const char* message);
extern "C" void* bionic_ANativeWindow_fromSurface(void* env, void* surface);

// CrashHandling.h seam: test entry points install the handlers themselves.
void installCrashHandlers(void);

#include "kudroid/KuArtRuntime.h"

// --- nh ngha nativeactivity ---

struct ANativeActivityCallbacks {
    void* onStart;
    void* onResume;
    void* onSaveInstanceState;
    void* onPause;
    void* onStop;
    void* onDestroy;
    void* onWindowFocusChanged;
    void* onNativeWindowCreated;
    void* onNativeWindowResized;
    void* onNativeWindowRedrawNeeded;
    void* onNativeWindowDestroyed;
    void* onInputQueueCreated;
    void* onInputQueueDestroyed;
    void* onContentRectChanged;
    void* onConfigurationChanged;
    void* onLowMemory;
};

struct ANativeActivity {
    ANativeActivityCallbacks* callbacks;
    JavaVM* vm;
    JNIEnv* env;
    jclass clazz;
    const char* internalDataPath;
    const char* externalDataPath;
    int32_t sdkVersion;
    void* instance;
    void* assetManager;
    const char* obbPath;
};

// KuART self-test: load framework.dex and exercise JNI both ways; arg kept for ABI compat.
extern "C" char* kudroid_test_jvm(const char* unused_path) {
    (void)unused_path;
    std::string log;
    appendTestHeader(log, "KuART Integration Test", "N/A");
    installCrashHandlers();

    log += "[kudroid_core] Phase: init KuART\n";
    kudroid::bionic_shim_reset_trace();

    static std::string* g_kuart_test_log = &log;
    g_kuart_test_log = &log;
    kuart_set_log_callback([](const char* msg) {
        if (g_kuart_test_log) {
            *g_kuart_test_log += "[KuART] ";
            *g_kuart_test_log += msg;
            *g_kuart_test_log += "\n";
        }
    });

    // app_dir rng = ch np framework.dex nhng.
    if (!kuart_init("")) {
        log += "[kudroid_core] ERROR: kuart_init failed: " + std::string(kuart_last_error()) + "\n";
        kuart_set_log_callback(nullptr);
        g_kuart_test_log = nullptr;
        return strdup(log.c_str());
    }
    log += "[kudroid_core] KuART DEX loaded: " + std::to_string(kuart_num_dex_files()) + "\n";

    JavaVM* vm = kuart_get_javavm();
    if (!vm) {
        log += "[kudroid_core] ERROR: JavaVM is null!\n";
        kuart_set_log_callback(nullptr);
        g_kuart_test_log = nullptr;
        return strdup(log.c_str());
    }

    JNIEnv* env = nullptr;
    kuart_get_env(vm, reinterpret_cast<void**>(&env), 0);

    if (env) {
        log += "[kudroid_core] Phase: testing JNI FindClass\n";
        // ActivityThread must load; success means the framework and linker work.
        for (const char* name : {"android/app/ActivityThread", "android/app/Activity",
                                 "android/os/Looper", "android/util/Log"}) {
            jclass c = env->FindClass(name);
            log += std::string("[kudroid_core] ") + (c ? "SUCCESS" : "FAILED ") +
                   ": FindClass(" + name + ")\n";
            if (env->ExceptionCheck()) env->ExceptionClear();
        }

        jclass at = env->FindClass("android/app/ActivityThread");
        if (at) {
            jmethodID main = env->GetStaticMethodID(at, "main", "([Ljava/lang/String;)V");
            log += std::string("[kudroid_core] ") + (main ? "SUCCESS" : "FAILED ") +
                   ": GetStaticMethodID(ActivityThread.main)\n";
        }

        jstring testStr = env->NewStringUTF("Hello KuART");
        if (testStr) {
            const char* utf = env->GetStringUTFChars(testStr, nullptr);
            log += "[kudroid_core] SUCCESS: Created JNI string: ";
            log += utf ? utf : "null";
            log += "\n";
            env->ReleaseStringUTFChars(testStr, utf);
        }
        log += "[kudroid_core] classes resolved: " +
               std::to_string(kuart_num_loaded_classes()) + "\n";
    } else {
        log += "[kudroid_core] ERROR: Failed to get JNIEnv!\n";
    }

    log += "[kudroid_core] Phase: shutdown KuART\n";
    kuart_shutdown();
    kuart_set_log_callback(nullptr);
    g_kuart_test_log = nullptr;

    log += "[kudroid_core] KuART test completed.\n";
    return strdup(log.c_str());
}

// Guest .so mappings must live for the process lifetime; unmapping while a render thread runs inside aborts.
kudroid::LibraryManager& globalLibraryManager() {
    static kudroid::LibraryManager instance;
    return instance;
}

// The directory kudroid_run_apk scanned for guest .so files.
//
// Kept because findLibrary() has to answer for a library that exists on disk but was
// never loaded — a lib listed in android.app.lib_name that failed to map still has a
// real path, and reporting null for it turns a load failure into a much vaguer
// "library not found" at the caller.
static std::mutex g_nativeLibDirMtx;
static std::string g_nativeLibDir;

extern "C" void kudroid_set_native_lib_dir(const char* dir) {
    if (dir == nullptr) return;
    std::lock_guard<std::mutex> lock(g_nativeLibDirMtx);
    g_nativeLibDir = dir;
}

extern "C" int kudroid_find_native_library(const char* name, char* out,
                                           unsigned long out_size) {
    if (name == nullptr || *name == '\0' || out == nullptr || out_size == 0) return 0;

    // Accept every form a caller might hold. android.app.lib_name gives the bare name
    // ("minecraftpe"), System.loadLibrary the same, while code that already has a file
    // name or a full path passes that instead. Normalising here means the Java side
    // does not have to guess which one it has.
    std::string base = std::filesystem::path(name).filename().string();
    if (base.empty()) return 0;
    std::string filename = base;
    if (filename.size() < 3 || filename.compare(filename.size() - 3, 3, ".so") != 0) {
        filename = "lib" + filename + ".so";
    }

    const auto emit = [&](const std::string& path) -> int {
        if (path.empty() || path.size() + 1 > out_size) return 0;
        std::memcpy(out, path.c_str(), path.size() + 1);
        return 1;
    };

    // A loaded library is the authoritative answer: its key is the canonical path the
    // ELF was actually mapped from, so it cannot disagree with reality.
    {
        kudroid::LibraryManager& manager = globalLibraryManager();
        for (const auto& pair : manager.libraries()) {
            if (std::filesystem::path(pair.first).filename().string() == filename) {
                return emit(pair.first);
            }
        }
    }

    // Not loaded: fall back to the scanned directory, but only if the file is there.
    // Returning a constructed path that does not exist would give the caller something
    // to fail on later rather than a clear miss now.
    std::string dir;
    {
        std::lock_guard<std::mutex> lock(g_nativeLibDirMtx);
        dir = g_nativeLibDir;
    }
    if (!dir.empty()) {
        std::error_code ec;
        const std::filesystem::path candidate = std::filesystem::path(dir) / filename;
        if (std::filesystem::exists(candidate, ec)) return emit(candidate.string());
    }
    return 0;
}

// Guest symbol hook used by bionic_dlsym for DUMMY_HANDLE.
extern "C" {
extern void* (*kudroid_guest_symbol_lookup)(const char* name);
extern void* (*kudroid_guest_library_open)(const char* filename);
extern void* (*kudroid_guest_library_symbol)(void* handle, const char* symbol);
extern int (*kudroid_guest_library_owns)(void* handle);
}

// ── Guest dlopen/dlsym/dlclose over LibraryManager-mapped .so files ─────────

// dlopen/dlsym/dlclose for guest .so files LibraryManager already mapped.
//
// A guest dlopen used to return DUMMY_HANDLE, and dlsym on that scans every loaded
// library and takes the first match — so a caller that named one library could get
// another's function whenever both export the same symbol. GameActivity is the caller
// that makes this concrete: it dlopens the .so from android.app.lib_name and reads its
// entry points out of that handle alone.
//
// The handle IS the ElfLoader pointer, kept in a registry so a pointer arriving from
// guest code can be validated before it is dereferenced — the same reason the JNI
// layer validates receivers rather than trusting them.

std::mutex& guestHandleMutex() {
    static std::mutex m;
    return m;
}

std::set<void*>& guestHandles() {
    static std::set<void*> handles;
    return handles;
}

void* guestLibraryOpen(const char* filename) {
    if (filename == nullptr || *filename == '\0') return nullptr;

    const std::string wanted = std::filesystem::path(filename).filename().string();
    if (wanted.empty()) return nullptr;

    kudroid::LibraryManager& manager = globalLibraryManager();
    for (const auto& pair : manager.libraries()) {
        if (std::filesystem::path(pair.first).filename().string() != wanted) continue;
        void* handle = pair.second.get();
        std::lock_guard<std::mutex> lock(guestHandleMutex());
        guestHandles().insert(handle);
        return handle;
    }

    // Load guest library on demand if present on disk.
    std::filesystem::path candidate(filename);
    std::error_code ec;
    if (!std::filesystem::exists(candidate, ec)) {
        std::string dir;
        {
            std::lock_guard<std::mutex> lock(g_nativeLibDirMtx);
            dir = g_nativeLibDir;
        }
        if (!dir.empty()) {
            candidate = std::filesystem::path(dir) / wanted;
        }
    }
    if (std::filesystem::exists(candidate, ec)) {
        if (manager.loadRecursive(candidate.string())) {
            for (const auto& pair : manager.libraries()) {
                if (std::filesystem::path(pair.first).filename().string() != wanted) continue;
                void* handle = pair.second.get();
                std::lock_guard<std::mutex> lock(guestHandleMutex());
                guestHandles().insert(handle);
                return handle;
            }
        }
    }
    return nullptr;
}

void* guestLibrarySymbol(void* handle, const char* symbol) {
    if (handle == nullptr || symbol == nullptr || *symbol == '\0') return nullptr;
    {
        std::lock_guard<std::mutex> lock(guestHandleMutex());
        if (guestHandles().count(handle) == 0) return nullptr;
    }
    return static_cast<kudroid::ElfLoader*>(handle)->getSymbolAddress(symbol);
}

int guestLibraryOwns(void* handle) {
    if (handle == nullptr) return 0;
    std::lock_guard<std::mutex> lock(guestHandleMutex());
    return guestHandles().count(handle) != 0 ? 1 : 0;
}



// ── Run entry ────────────────────────────────────────────────────────────────

extern "C" void kudroid_gpu_note_run_end(void);

extern "C" const char* kudroid_run_apk(const char* appName) {
    if (s_isApkRunning.exchange(true)) {
        kudroid_android_log_message(3, "kudroid_core", "kudroid_run_apk: APK is already running in background, ignoring duplicate launch request.");
        return strdup("[kudroid_core] APK is already running.\n");
    }

    // Stale native mappings survive installs (unmapping under detached threads
    // aborts), so a post-install run in this process would mix old code with new
    // files. Refuse with instructions instead of crashing mysteriously.
    if (s_libsResident.load() && s_libsGeneration.load() != s_installGeneration.load()) {
        s_isApkRunning.store(false);
        return strdup("[kudroid_core] ERROR: installed or removed an app since the last run.\n"
                      "[kudroid_core] Native libraries from before are still mapped in this process.\n"
                      "[kudroid_core] Please restart KuDroidShell, then run again.\n");
    }

    // Clear all old logs before launching guest app so logs start fresh for this run
    kudroid_clear_all_logs();

    // Fresh frame-liveness clock: the watchdog's frame-silence trigger must
    // measure THIS run's presents, not the previous session's last swap.
    kudroid_frame_presented_reset();

    kudroid::native_run_begin();
    kudroid::native_phase("apk-run-enter");
    // Re-arm teardown: a wedged run may never reach run end to reset it.
    s_stopping.store(false);

    std::string log;
    appendTestHeader(log, "Run APK Native Libraries", appName);
    kudroid::bionic_shim_reset_trace();

    // JIT permission is a precondition for launching, not a mode to degrade into.
    //
    // Guest .so files are mapped RW and then mprotect'd to add PROT_EXEC. iOS grants
    // PROT_EXEC on anonymous memory only to a debugged process or one holding the JIT
    // entitlement, and it will never grant it on an unsigned file mapping — which every
    // guest .so is. So without the permission no guest native code can run, and there
    // is no workaround at this layer: it is a code-signing rule, not missing code.
    //
    // KuDroid targets Android apps with NDK libraries, where that means the app cannot
    // start at all. Refusing here with instructions is more useful than loading the
    // Java side and failing later at the first native call, and it avoids mapping
    // hundreds of megabytes that are certain to be discarded.
    //
    // Everything downstream of this gate still keeps its interpreter path: most Dex
    // methods are interpreted even with JIT available (JitCompiler covers a small
    // opcode subset), java.lang natives run from LibCore inside this signed binary,
    // and JitCache stays null-safe. That is how the runtime works, not a fallback.
    if (kudroid_is_jit_enabled() == 0) {
        log +=
            "[kudroid_core] ERROR: JIT is not enabled, cannot launch.\n"
            "[kudroid_core]        Android apps ship native libraries (.so) that must be\n"
            "[kudroid_core]        mapped executable. iOS refuses that without JIT\n"
            "[kudroid_core]        permission, so the app cannot start.\n"
            "[kudroid_core]        Enable JIT, then launch again:\n"
            "[kudroid_core]          - LiveContainer: turn on JIT for this app\n"
            "[kudroid_core]          - Sideloaded: attach StikDebug / SideStore\n"
            "[kudroid_core]          - TrollStore: install from TrollStore\n";
        std::fputs(log.c_str(), stderr);
        logCoreLine(6, "[kudroid_core] launch refused: JIT not enabled");
        mirrorCrash(log);
        // Released here: the launch never began, so a retry after enabling JIT must not
        // be rejected as a duplicate by the guard at the top of this function.
        s_isApkRunning.store(false);
        return strdup(log.c_str());
    }

    log += "[kudroid_core] Phase: init LibraryManager\n";

    if (!appName || !*appName) {
        log += "[kudroid_core] ERROR: null or empty app name\n";
    } else {
        auto& remapper = kudroid::VFSPathRemapper::getInstance();
        std::string resolvedAppName = appName;
        std::filesystem::path appDir = std::filesystem::path(remapper.androidRoot()) / "data/app" / resolvedAppName;

        // Standardize the install dir to the real package ID.
        {
            const auto mfPath = appDir / "AndroidManifest.xml";
            std::string pkgId;
            if (std::filesystem::exists(mfPath)) {
                std::ifstream mf(mfPath, std::ios::binary);
                if (mf) {
                    std::vector<std::uint8_t> axml(
                        (std::istreambuf_iterator<char>(mf)),
                        std::istreambuf_iterator<char>());
                    kudroid::ManifestInfo mi =
                        kudroid::APKExtractor::parse_manifest(axml.data(), axml.size());
                    if (mi.packageName.empty() && axml.size() > 4 && axml[0] == '<') {
                        mi = kudroid::APKExtractor::parse_manifest_text(
                            reinterpret_cast<const char*>(axml.data()), axml.size());
                    }
                    pkgId = mi.packageName;
                }
            }

            if (!pkgId.empty() && pkgId != resolvedAppName) {
                const auto cleanAppDir =
                    std::filesystem::path(remapper.androidRoot()) / "data/app" / pkgId;
                std::error_code renEc;
                bool movedApp = false;
                if (!std::filesystem::exists(cleanAppDir)) {
                    std::filesystem::rename(appDir, cleanAppDir, renEc);
                    movedApp = !renEc;
                }
                // Dalvik-cache c ca ng dex2jar phi theo tn mi bc
                // dn dp after ny tm thy v remove c.
                const auto oldCache = std::filesystem::path(remapper.androidRoot()) /
                                      "data/dalvik-cache" / resolvedAppName;
                const auto newCache = std::filesystem::path(remapper.androidRoot()) /
                                      "data/dalvik-cache" / pkgId;
                if (std::filesystem::exists(oldCache) && !std::filesystem::exists(newCache)) {
                    std::error_code cacheEc;
                    std::filesystem::rename(oldCache, newCache, cacheEc);
                }
                if (movedApp) {
                    resolvedAppName = pkgId;
                    appDir = cleanAppDir;
                } else if (std::filesystem::exists(cleanAppDir)) {
                    // Already standard; keep as is.
                }
            }
        }

        // Fallback: when the manifest is unreadable, use the package ID from app_info.json.
        if (std::filesystem::exists(appDir)) {
            std::filesystem::path infoPath = appDir / "app_info.json";
            if (std::filesystem::exists(infoPath)) {
                std::ifstream f(infoPath);
                std::string line;
                while (std::getline(f, line)) {
                    auto pos = line.find("\"package\": \"");
                    if (pos != std::string::npos) {
                        auto start = pos + 12;
                        auto end = line.find("\"", start);
                        if (end != std::string::npos) {
                            std::string pkg = line.substr(start, end - start);
                            if (!pkg.empty() && pkg != resolvedAppName) {
                                std::filesystem::path cleanTarget = std::filesystem::path(remapper.androidRoot()) / "data/app" / pkg;
                                std::error_code ec;
                                std::filesystem::rename(appDir, cleanTarget, ec);
                                if (!ec) {
                                    resolvedAppName = pkg;
                                    appDir = cleanTarget;
                                }
                            }
                        }
                    }
                }
            }
        } else {
            auto uIdx = resolvedAppName.find('_');
            if (uIdx != std::string::npos) {
                std::string base = resolvedAppName.substr(0, uIdx);
                std::filesystem::path baseDir = std::filesystem::path(remapper.androidRoot()) / "data/app" / base;
                if (std::filesystem::exists(baseDir)) {
                    resolvedAppName = base;
                    appDir = baseDir;
                }
            }
        }

        // Android standard environment variables
        ::setenv("ANDROID_ROOT", "/system", 1);
        ::setenv("ANDROID_DATA", "/data", 1);
        ::setenv("ANDROID_STORAGE", "/storage", 1);
        ::setenv("EXTERNAL_STORAGE", "/sdcard", 1);
        ::setenv("TMPDIR", "/data/local/tmp", 1);

        std::filesystem::path libDir = appDir / ("lib/" KUDROID_DEVICE_ABI);
        if (!std::filesystem::exists(libDir)) {
            const auto aospLibDir = appDir / "lib/arm64";
            if (std::filesystem::exists(aospLibDir)) {
                libDir = aospLibDir;
            }
        }
        const std::filesystem::path assetsDir = appDir / "assets";
        kudroid_set_assets_dir(assetsDir.string().c_str());
        kudroid_set_native_lib_dir(libDir.string().c_str());

        auto appendAndEcho = [&](const std::string& line) {
            log += line + "\n";
            std::fprintf(stderr, "%s\n", line.c_str());
            logCoreLine(2, line);
            mirrorCrash(log);
        };

        appendAndEcho("[kudroid_core] Scanning library directory: " + libDir.string());

        if (!std::filesystem::exists(libDir)) {
            appendAndEcho("[kudroid_core] ERROR: Library directory does not exist: " + libDir.string());
        } else {
            kudroid::LibraryManager& manager = globalLibraryManager();

            // Hook guest symbol lookup for bionic_dlsym(DUMMY_HANDLE, ...).
            kudroid_guest_symbol_lookup = [](const char* name) -> void* {
                return globalLibraryManager().resolveGlobalSymbol(name);
            };

            // Per-library dlopen/dlsym for the guest's own .so files. Installed here
            // rather than at load time because it must be live before any guest code
            // runs, and this is the point where the libraries exist.
            kudroid_guest_library_open = &guestLibraryOpen;
            kudroid_guest_library_symbol = &guestLibrarySymbol;
            kudroid_guest_library_owns = &guestLibraryOwns;

            // KuART must be ready before dlopening anything: static initializers may query the VM.
            kuart_set_log_callback([](const char* msg) {
                kudroid_android_log_message(4, "KuART", msg);
                std::fprintf(stderr, "[KuART] %s\n", msg);
            });
            kuart_set_symbol_lookup([](const char* symbol) -> void* {
                return globalLibraryManager().resolveGlobalSymbol(symbol);
            });

            if (kuart_init(appDir.string().c_str())) {
                appendAndEcho("[kudroid_core] KuART ready: " +
                              std::to_string(kuart_num_dex_files()) + " DEX loaded");
            } else {
                appendAndEcho("[kudroid_core] ERROR: KuART init failed: " +
                              std::string(kuart_last_error()));
            }

            std::vector<std::string> soFiles;
            for (const auto& entry : std::filesystem::directory_iterator(libDir)) {
                if (entry.path().extension() == ".so") {
                    soFiles.push_back(entry.path().string());
                }
            }

            if (soFiles.empty()) {
                appendAndEcho("[kudroid_core] WARNING: No .so files found in " + libDir.string());
            }
            // Not an `else`: everything below — the manifest parse, KuART launch and
            // ActivityThread.main — has to run even when there are no native libraries
            // to load. It used to sit in the else branch, so an app with no usable .so
            // (an APK built for another ABI, say) silently never started its Java side
            // either. The load loop below is a no-op on an empty list.
            {
                for (const auto& soPath : soFiles) {
                    appendAndEcho("[kudroid_core] Attempting to load: " + soPath);
                    if (!manager.loadRecursive(soPath.c_str())) {
                        appendAndEcho("[kudroid_core] LOAD FAILED for " + soPath + ": " + manager.lastError());
                    } else {
                        appendAndEcho("[kudroid_core] LOAD SUCCESS for " + soPath);
                        kudroid_boot_mark(
                            ("elf-loaded " + std::filesystem::path(soPath).filename().string())
                                .c_str());
                    }
                }

                appendAndEcho("[kudroid_core] Total loaded libraries: " + std::to_string(manager.libraries().size()));
                for (const auto& pair : manager.libraries()) {
                    char mapLine[256];
                    snprintf(mapLine, sizeof(mapLine), "  %s -> %p", pair.first.c_str(), pair.second->baseAddress());
                    appendAndEcho(mapLine);
                }
                // Resident from here on; a later install taints the next run.
                s_libsResident.store(true);
                s_libsGeneration.store(s_installGeneration.load());

                JavaVM* jvm = kuart_get_javavm();
                if (!jvm) {
                    appendAndEcho("[kudroid_core] ERROR: KuART failed to initialize.");
                } else {
                    char jvmLine[128];
                    snprintf(jvmLine, sizeof(jvmLine), "[kudroid_core] KuART ready (JavaVM=%p).", (void*)jvm);
                    appendAndEcho(jvmLine);

                    auto native_activity_create = reinterpret_cast<void (*)(ANativeActivity*, void*, size_t)>(
                        manager.resolveAppSymbol("ANativeActivity_onCreate")
                    );

                    static std::set<std::string> s_loadedJniOnLoads;
                    auto invokeJniOnLoad = [](const std::string& libname) -> int {
                        if (libname.empty()) return 0;
                        std::string name = libname;
                        std::string filename = name;
                        if (filename.find(".so") == std::string::npos) {
                            filename = "lib" + filename + ".so";
                        }
                        kudroid::LibraryManager& manager = globalLibraryManager();
                        void* sym = manager.resolveSymbolInLib(filename, "JNI_OnLoad");
                        if (!sym) sym = manager.resolveSymbolInLib(name, "JNI_OnLoad");
                        if (sym) {
                            if (s_loadedJniOnLoads.insert(filename).second) {
                                JavaVM* jvm = kuart_get_javavm();
                                if (jvm) {
                                    bionic_init_main_thread_tls();
                                    char msg[256];
                                    snprintf(msg, sizeof(msg), "[kudroid_core] Invoking JNI_OnLoad in %s", filename.c_str());
                                    logCoreLine(4, msg);
                                    std::fprintf(stderr, "%s\n", msg);

                                    auto jni_onload = reinterpret_cast<jint (*)(JavaVM*, void*)>(sym);
                                    jint version = 0;
                                    const int guardRc = kudroid_call_jni_onload_guarded(jni_onload, jvm, &version);
                                    if (guardRc == 0) {
                                        snprintf(msg, sizeof(msg), "[kudroid_core] JNI_OnLoad(%s) returned version: %d", filename.c_str(), version);
                                        logCoreLine(4, msg);
                                        std::fprintf(stderr, "%s\n", msg);
                                        kudroid_boot_mark(
                                            ("jni-onload " + std::filesystem::path(filename).filename().string())
                                                .c_str());
                                    } else if (guardRc < 0) {
                                        snprintf(msg, sizeof(msg), "[kudroid_core] WARNING: Native exception in JNI_OnLoad for %s", filename.c_str());
                                        logCoreLine(5, msg);
                                        std::fprintf(stderr, "%s\n", msg);
                                        const std::string report =
                                            describeJniGuardFault(filename, guardRc);
                                        std::fputs(report.c_str(), stderr);
                                        appendCrashLogFile(report);
                                    } else {
                                        snprintf(msg, sizeof(msg), "[kudroid_core] WARNING: JNI_OnLoad in %s raised fatal signal %d", filename.c_str(), guardRc);
                                        logCoreLine(5, msg);
                                        std::fprintf(stderr, "%s\n", msg);
                                        const std::string report =
                                            describeJniGuardFault(filename, guardRc);
                                        std::fputs(report.c_str(), stderr);
                                        appendCrashLogFile(report);
                                    }

                                    const char* leaked = nullptr;
                                    if (kuart_take_pending_exception(&leaked)) {
                                        snprintf(msg, sizeof(msg),
                                                 "[kudroid_core] JNI_OnLoad(%s) left a pending Java exception; cleared",
                                                 filename.c_str());
                                        logCoreLine(5, msg);
                                        std::fprintf(stderr, "%s:\n%s\n", msg,
                                                     leaked != nullptr ? leaked : "?");
                                    }
                                }
                            }
                        }
                        return 1;
                    };

                    kuart_set_load_library_callback([](const char* libname) -> int {
                        if (!libname || !*libname) return 0;
                        kudroid::LibraryManager& manager = globalLibraryManager();
                        std::string filename = libname;
                        if (filename.find(".so") == std::string::npos) {
                            filename = "lib" + filename + ".so";
                        }
                        void* sym = manager.resolveSymbolInLib(filename, "JNI_OnLoad");
                        if (!sym) sym = manager.resolveSymbolInLib(libname, "JNI_OnLoad");
                        if (sym) {
                            static std::set<std::string> s_dynamicJniOnLoads;
                            if (s_dynamicJniOnLoads.insert(filename).second) {
                                JavaVM* jvm = kuart_get_javavm();
                                if (jvm) {
                                    bionic_init_main_thread_tls();
                                    char msg[256];
                                    snprintf(msg, sizeof(msg), "[kudroid_core] Invoking JNI_OnLoad in %s (System.loadLibrary)", filename.c_str());
                                    logCoreLine(4, msg);
                                    std::fprintf(stderr, "%s\n", msg);

                                    auto jni_onload = reinterpret_cast<jint (*)(JavaVM*, void*)>(sym);
                                    jint version = 0;
                                    kudroid_call_jni_onload_guarded(jni_onload, jvm, &version);
                                }
                            }
                        }
                        return 1;
                    });

                    if (native_activity_create) {
                        appendAndEcho("[kudroid_core] Native Game Activity detected.");

                        appendAndEcho("[kudroid_core] Found ANativeActivity_onCreate, invoking...");

                        static std::string s_internalDataPath;
                        static std::string s_externalDataPath;
                        static std::string s_obbPath;
                        s_internalDataPath = "/data/data/" + resolvedAppName;
                        s_externalDataPath = "/sdcard/Android/data/" + resolvedAppName;
                        s_obbPath = "/sdcard/Android/obb/" + resolvedAppName;

                        std::error_code vfsEc;
                        const auto rootPath = std::filesystem::path(remapper.androidRoot());
                        std::filesystem::create_directories(rootPath / "data/data" / resolvedAppName / "files", vfsEc);
                        std::filesystem::create_directories(rootPath / "data/data" / resolvedAppName / "cache", vfsEc);
                        std::filesystem::create_directories(rootPath / "data/data" / resolvedAppName / "shared_prefs", vfsEc);
                        std::filesystem::create_directories(rootPath / "data/data" / resolvedAppName / "databases", vfsEc);
                        std::filesystem::create_directories(rootPath / "data/data" / resolvedAppName / "code_cache", vfsEc);
                        std::filesystem::create_directories(rootPath / "sdcard/Android/data" / resolvedAppName / "files", vfsEc);
                        std::filesystem::create_directories(rootPath / "sdcard/Android/data" / resolvedAppName / "cache", vfsEc);
                        std::filesystem::create_directories(rootPath / "sdcard/Android/obb" / resolvedAppName, vfsEc);

                        static ANativeActivityCallbacks mock_callbacks = {};
                        static ANativeActivity mock_activity = {
                            &mock_callbacks,
                            jvm,
                            nullptr, // env
                            nullptr, // clazz
                            s_internalDataPath.c_str(),
                            s_externalDataPath.c_str(),
                            KUDROID_SDK_INT, // must match Build.VERSION.SDK_INT
                            nullptr, // instance
                            nullptr, // assetManager
                            s_obbPath.c_str()
                        };
                        kuart_get_env(jvm, reinterpret_cast<void**>(&mock_activity.env), 0);

                        if (mock_activity.env) {
                            JNIEnv* env = mock_activity.env;
                            if (mock_activity.assetManager) {
                                env->DeleteGlobalRef(static_cast<jobject>(mock_activity.assetManager));
                                mock_activity.assetManager = nullptr;
                            }
                            jclass assetCls = env->FindClass("android/content/res/AssetManager");
                            if (assetCls && !env->ExceptionCheck()) {
                                jmethodID ctor = env->GetMethodID(assetCls, "<init>", "()V");
                                jobject am = ctor ? env->NewObject(assetCls, ctor) : nullptr;
                                if (am) {
                                    mock_activity.assetManager = env->NewGlobalRef(am);
                                    env->DeleteLocalRef(am);
                                    appendAndEcho("[kudroid_core] AssetManager jobject created.");
                                }
                                if (env->ExceptionCheck()) env->ExceptionClear();
                                env->DeleteLocalRef(assetCls);
                            } else {
                                if (env->ExceptionCheck()) env->ExceptionClear();
                            }
                        }

                        native_activity_create(&mock_activity, nullptr, 0);
                        appendAndEcho("[kudroid_core] ANativeActivity_onCreate completed.");

                        auto call1 = [&](const char* name, void* fn) {
                            if (!fn) return;
                            appendAndEcho(std::string("[kudroid_core] Calling ") + name);
                            reinterpret_cast<void (*)(ANativeActivity*)>(fn)(&mock_activity);
                        };
                        auto call2 = [&](const char* name, void* fn, void* arg2) {
                            if (!fn) return;
                            appendAndEcho(std::string("[kudroid_core] Calling ") + name);
                            reinterpret_cast<void (*)(ANativeActivity*, void*)>(fn)(&mock_activity, arg2);
                        };
                        call1("onStart", mock_callbacks.onStart);
                        call1("onResume", mock_callbacks.onResume);
                        call2("onWindowFocusChanged", mock_callbacks.onWindowFocusChanged, reinterpret_cast<void*>(1));
                        call2("onNativeWindowCreated", mock_callbacks.onNativeWindowCreated, bionic_ANativeWindow_fromSurface(nullptr, nullptr));
                        call2("onInputQueueCreated", mock_callbacks.onInputQueueCreated, kudroid_get_input_queue());
                        appendAndEcho("[kudroid_core] Lifecycle callbacks invoked successfully!");
                    } else {
                        appendAndEcho("[kudroid_core] Java APK Application detected (No ANativeActivity_onCreate).");

                        std::string targetActivity = "";
                        std::string pkgName = "";
                        // Everything the manifest declares. Populated below and used
                        // as the fallback list, so KuDroid only ever tries classes the
                        // app actually declares.
                        std::vector<std::string> manifestActivities;
                        std::string manifestAppClass;
                        // android:appComponentFactory. Android instantiates it before
                        // any component, so its <clinit> is the first guest code to
                        // run; skipping it leaves whatever the app initialises there
                        // empty. Generic: whatever the manifest names, nothing assumed.
                        std::string manifestComponentFactory;
                        // <meta-data> per activity plus the <application> block. Kept
                        // whole rather than reduced to what one app needs: a component
                        // reads its own manifest entry while constructing itself, so
                        // whatever key it wants has to already be there.
                        kudroid::ManifestInfo manifestInfo;

                        // Priority 1: parse the extracted manifest; app_info.json may be stale.
                        const auto manifestPath = appDir / "AndroidManifest.xml";
                        if (std::filesystem::exists(manifestPath)) {
                            std::ifstream mf(manifestPath, std::ios::binary);
                            if (mf) {
                                std::vector<std::uint8_t> axml(
                                    (std::istreambuf_iterator<char>(mf)),
                                    std::istreambuf_iterator<char>());
                                appendAndEcho("[kudroid_core] Parsing AndroidManifest.xml (" +
                                              std::to_string(axml.size()) + " bytes)...");
                                kudroid::ManifestInfo mi =
                                    kudroid::APKExtractor::parse_manifest(axml.data(), axml.size());
                                // APK repack (apktool/BANDISHARE...) thng cha
                                // manifest dng TEXT — AXML parser tr rng vi
                                // chng. Th parse text before khi b cuc.
                                if (mi.mainActivity.empty() && axml.size() > 4 &&
                                    axml[0] == '<') {
                                    appendAndEcho("[kudroid_core] Manifest is TEXT XML, trying text parser...");
                                    mi = kudroid::APKExtractor::parse_manifest_text(
                                        reinterpret_cast<const char*>(axml.data()), axml.size());
                                }
                                appendAndEcho("[kudroid_core] Manifest parse: package='" + mi.packageName +
                                              "' mainActivity='" + mi.mainActivity + "'" +
                                              " activities=" + std::to_string(mi.activities.size()) +
                                              (mi.appClass.empty() ? "" : " application='" + mi.appClass + "'") +
                                              (mi.appComponentFactory.empty()
                                                   ? ""
                                                   : " appComponentFactory='" + mi.appComponentFactory + "'"));
                                if (!mi.mainActivity.empty()) targetActivity = mi.mainActivity;
                                if (!mi.packageName.empty()) pkgName = mi.packageName;
                                manifestAppClass = mi.appClass;
                                manifestComponentFactory = mi.appComponentFactory;
                                // The manifest is the authoritative list of what this
                                // app can launch: launcher activities first, then the
                                // rest. Feeding these to ActivityThread replaces the
                                // old habit of inventing names like "<pkg>.Main",
                                // which could never exist unless the app happened to
                                // use that exact name.
                                manifestActivities = mi.launchOrder();
                                for (const auto& a : mi.activities) {
                                    appendAndEcho(std::string("[kudroid_core]   manifest activity: ") + a.name +
                                                  (a.isLauncher ? "  [LAUNCHER]" : "") +
                                                  (a.isAlias ? "  [alias]" : "") +
                                                  (a.screenOrientation >= 0 ? "  [orientation=" + (a.screenOrientationStr.empty() ? std::to_string(a.screenOrientation) : a.screenOrientationStr) + "]" : "") +
                                                  (a.metaData.empty()
                                                       ? ""
                                                       : "  [" + std::to_string(a.metaData.size()) +
                                                             " meta-data]"));
                                    if ((a.isLauncher || a.name == targetActivity) && a.screenOrientation >= 0) {
                                        kudroid_set_requested_orientation(a.screenOrientation);
                                    }
                                }
                                manifestInfo = mi;
                            } else {
                                appendAndEcho("[kudroid_core] WARNING: Cannot open AndroidManifest.xml");
                            }
                        } else {
                            appendAndEcho("[kudroid_core] WARNING: AndroidManifest.xml not found in " + appDir.string());
                        }

                        // U TIN 2: app_info.json do extractor write lc install.
                        if (targetActivity.empty()) {
                            std::filesystem::path infoPath = appDir / "app_info.json";
                            if (std::filesystem::exists(infoPath)) {
                                std::ifstream f(infoPath);
                                std::string line;
                                while (std::getline(f, line)) {
                                    auto posAct = line.find("\"main_activity\": \"");
                                    if (posAct != std::string::npos) {
                                        auto start = posAct + 18;
                                        auto end = line.find("\"", start);
                                        if (end != std::string::npos) {
                                            std::string act = line.substr(start, end - start);
                                            if (!act.empty()) targetActivity = act;
                                        }
                                    }
                                    auto posPkg = line.find("\"package\": \"");
                                    if (posPkg != std::string::npos) {
                                        auto start = posPkg + 12;
                                        auto end = line.find("\"", start);
                                        if (end != std::string::npos) {
                                            pkgName = line.substr(start, end - start);
                                        }
                                    }
                                }
                            }
                        }

                        // Priority 3: scan app DEX classes for Activities (no classes.jar needed).
                        std::vector<std::string> verifiedActivities;
                        if (targetActivity.empty()) {
                            std::vector<std::string> classes;
                            {
                                constexpr size_t kMaxClasses = 20000;
                                std::vector<char*> raw(kMaxClasses, nullptr);
                                const size_t n = kuart_list_app_classes(raw.data(), kMaxClasses);
                                classes.reserve(n);
                                for (size_t i = 0; i < n; ++i) classes.emplace_back(raw[i]);
                                kuart_free_class_list(raw.data(), n);
                            }
                            {
                                appendAndEcho("[kudroid_core] Scanning app DEX for Activity classes...");
                                appendAndEcho("[kudroid_core] Found " + std::to_string(classes.size()) +
                                              " non-system classes in app DEX");
                                // Heuristics to pick launcher:
                                // -1000 Activity of 3rd party SDK (push/analytics, etc.) — should not be entry point,
                                // launching them just shows blank screen (lesson learned from Braze).
                                // +100 name contains "Activity"
                                // +50 package matches pkgName (from manifest/app_info)
                                // +depth bonus: shorter package (app root) is more likely the main entry point.
                                //
                                // Prefixes, not substrings, and only packages that
                                // genuinely belong to an embedded SDK.
                                //
                                // A substring test rejects any class whose name merely
                                // CONTAINS one of these, wherever it appears: an app at
                                // com.google.* lost 1000 points for its own launcher,
                                // and so did every app with "adjust" or "amplitude"
                                // anywhere in a class name. Two entries were not SDKs at
                                // all — "zarchiver" is an app (ru.zdevs.zarchiver), and
                                // "unity3d" covers com.unity3d.player.UnityPlayerActivity
                                // which IS the launcher of essentially every Unity game.
                                static const char* kSdkActivityPrefixes[] = {
                                    "com/braze/", "com/appboy/", "com/facebook/",
                                    "com/firebase/", "com/google/firebase/",
                                    "com/google/android/gms/", "com/google/ads/",
                                    "com/google/android/play/", "com/appsflyer/",
                                    "com/adjust/sdk/", "com/amplitude/", "com/mixpanel/",
                                    "com/crashlytics/", "io/sentry/", "com/playfab/",
                                    "com/microsoft/appcenter/", "com/android/billingclient/",
                                    "com/onesignal/", "com/urbanairship/",
                                };
                                // Calculate score ONCE instead of inside loop for every
                                // class (large JARs can have 10k+ classes).
                                std::string pkgPrefix;
                                if (!pkgName.empty()) {
                                    // pkgName dng a.b.c → prefix "a/b/c/".
                                    pkgPrefix = pkgName;
                                    for (char& c : pkgPrefix) if (c == '.') c = '/';
                                    pkgPrefix += '/';
                                }
                                // A class inside the app's OWN package is never an
                                // embedded SDK, whatever it is called. This is what
                                // keeps an app published under com.google.* — or one
                                // that vendors an SDK into its own namespace — from
                                // blacklisting its own launcher.
                                auto isSdkOwned = [&pkgPrefix](const std::string& cls) {
                                    if (!pkgPrefix.empty() &&
                                        cls.compare(0, pkgPrefix.size(), pkgPrefix) == 0) {
                                        return false;
                                    }
                                    for (const char* prefix : kSdkActivityPrefixes) {
                                        if (cls.rfind(prefix, 0) == 0) return true;
                                    }
                                    return false;
                                };
                                std::string best;
                                int bestScore = -1;
                                std::string bestAny; // fallback when no *Activity found
                                int bestAnyScore = -1;
                                for (const auto& cls : classes) {
                                    const bool isActivity = cls.find("Activity") != std::string::npos;
                                    const size_t depth = static_cast<size_t>(
                                        std::count(cls.begin(), cls.end(), '/'));
                                    int score = (isActivity ? 100 : 0) + static_cast<int>(10 - depth);
                                    // Launcher activities are usually named "...MainActivity".
                                    if (cls.size() >= 13 &&
                                        cls.compare(cls.size() - 13, 13, "/MainActivity") == 0) score += 80;
                                    if (!pkgPrefix.empty() &&
                                        cls.compare(0, pkgPrefix.size(), pkgPrefix) == 0) score += 50;
                                    if (isSdkOwned(cls)) score -= 1000;

                                    if (isActivity && score > bestScore) {
                                        bestScore = score;
                                        best = cls;
                                    }
                                    if (score > bestAnyScore) {
                                        bestAnyScore = score;
                                        bestAny = cls;
                                    }
                                }
                                if (!best.empty() && bestScore > 0) {
                                    for (char& c : best) if (c == '/') c = '.';
                                    targetActivity = best;
                                    appendAndEcho("[kudroid_core] Resolved from app DEX: " + best);
                                } else if (!bestAny.empty() && bestAnyScore > 0) {
                                    // No clean *Activity — use the highest-scored class.
                                    for (char& c : bestAny) if (c == '/') c = '.';
                                    targetActivity = bestAny;
                                    appendAndEcho("[kudroid_core] No clean *Activity class; using highest-scored app class: " + bestAny);
                                } else if (!classes.empty()) {
                                    // Every class was SDK-penalised — log a few for debugging.
                                    for (size_t i = 0; i < classes.size() && i < 5; ++i) {
                                        appendAndEcho("[kudroid_core]   candidate(all-SDK?): " + classes[i]);
                                    }
                                    std::string first = classes.front();
                                    for (char& c : first) if (c == '/') c = '.';
                                    targetActivity = first;
                                    appendAndEcho("[kudroid_core] Using first app class as last resort: " + first);
                                }

                                // Verify the candidate really extends Activity (ProGuard may obfuscate names).
                                if (!targetActivity.empty() &&
                                    kuart_class_extends_activity(targetActivity.c_str()) != 1) {
                                    appendAndEcho("[kudroid_core] Candidate '" + targetActivity +
                                                  "' does NOT extend Activity (obfuscated?). Verifying all classes...");
                                    targetActivity.clear();
                                    int verifiedBestScore = -1;
                                    std::string verifiedBest;
                                    int checked = 0;
                                    // Collect real candidate Activities as fallbacks for ActivityThread if the primary
                                    // candidate fails. Two-pass scan: Pass 0 checks names containing 'Activity',
                                    // Pass 1 inspects remaining classes (bounded at 2000 items for large DEX).
                                    for (int pass = 0; pass < 2 && verifiedBestScore <= 0; ++pass) {
                                        int checkedThisPass = 0;
                                        for (const auto& cls : classes) {
                                            const bool hasName = cls.find("Activity") != std::string::npos;
                                            if (pass == 0 && !hasName) continue;
                                            if (pass == 1 && hasName) continue;
                                            if (++checkedThisPass > (pass == 0 ? 8000 : 2000)) break;
                                            std::string dotted = cls;
                                            for (char& c : dotted) if (c == '/') c = '.';
                                            ++checked;
                                            if (kuart_class_extends_activity(dotted.c_str()) != 1) continue;
                                            // Real Activity candidate — calculate relevance score.
                                            const size_t depth = static_cast<size_t>(
                                                std::count(cls.begin(), cls.end(), '/'));
                                            int score = 100 + static_cast<int>(10 - depth);
                                            if (cls.size() >= 13 &&
                                                cls.compare(cls.size() - 13, 13, "/MainActivity") == 0) score += 80;
                                            if (!pkgPrefix.empty() &&
                                                cls.compare(0, pkgPrefix.size(), pkgPrefix) == 0) score += 50;
                                            if (isSdkOwned(cls)) score -= 1000;
                                            appendAndEcho("[kudroid_core]   Activity candidate: " + dotted +
                                                          " (score=" + std::to_string(score) + ")");
                                            verifiedActivities.push_back(dotted);
                                            if (score > verifiedBestScore) {
                                                verifiedBestScore = score;
                                                verifiedBest = dotted;
                                            }
                                            if (verifiedBestScore >= 185) break; // pkg match + MainActivity — high confidence match
                                        }
                                    }
                                    appendAndEcho("[kudroid_core] Activity verify done: " +
                                                  std::to_string(checked) + " classes checked");
                                    if (!verifiedBest.empty() && verifiedBestScore > 0) {
                                        targetActivity = verifiedBest;
                                        appendAndEcho("[kudroid_core] Verified best Activity: " + verifiedBest);
                                    } else if (!verifiedBest.empty()) {
                                        // All candidates belong to SDKs — select best-scoring candidate with a warning.
                                        targetActivity = verifiedBest;
                                        appendAndEcho("[kudroid_core] WARNING: only SDK Activities found; using least-bad: " + verifiedBest);
                                    } else {
                                        appendAndEcho("[kudroid_core] WARNING: no class in app DEX extends android.app.Activity");
                                    }
                                } else if (!targetActivity.empty()) {
                                    appendAndEcho("[kudroid_core] Verified: '" + targetActivity + "' extends Activity ✓");
                                }
                            }
                        }

                        // PRIORITY 4: heuristic guessing from package name only when prior sources fail.
                        // Prefer any manifest-declared activity over a guess: the
                        // manifest is what Android itself reads.
                        if (targetActivity.empty() && !manifestActivities.empty()) {
                            targetActivity = manifestActivities.front();
                            appendAndEcho("[kudroid_core] Using first manifest-declared activity: " +
                                          targetActivity);
                        }
                        if (targetActivity.empty()) {
                            if (!pkgName.empty()) {
                                targetActivity = pkgName + ".MainActivity";
                            } else {
                                std::string base = appName;
                                auto uIdx = base.find('_');
                                if (uIdx != std::string::npos) {
                                    base = base.substr(0, uIdx);
                                }
                                targetActivity = base + ".MainActivity";
                            }
                            appendAndEcho("[kudroid_core] WARNING: guessed Activity '" + targetActivity +
                                          "' (manifest/dex-scan/JNI lookup failed)");
                        }

                        // Fallback list for ActivityThread, in descending order of
                        // authority:
                        //   1. activities the MANIFEST declares (launcher first),
                        //   2. classes verified to extend android.app.Activity.
                        //
                        // Names are no longer invented from the package. Guessed
                        // candidates like "<pkg>.Main" only ever produced
                        // ClassNotFoundException — an app either declares an activity
                        // in its manifest or it cannot be launched, which is exactly
                        // how Android decides. The only guess left is the last-resort
                        // targetActivity above, used when there is no manifest at all.
                        std::vector<std::string> fallbackStorage;
                        auto addFallback = [&](const std::string& s) {
                            if (s.empty() || s == targetActivity) return;
                            for (const auto& f : fallbackStorage)
                                if (f == s) return;
                            if (fallbackStorage.size() < 12) fallbackStorage.push_back(s);
                        };
                        for (const auto& a : manifestActivities) addFallback(a);
                        for (const auto& v : verifiedActivities) addFallback(v);
                        std::vector<const char*> fallbackPtrs;
                        for (const auto& f : fallbackStorage) fallbackPtrs.push_back(f.c_str());

                        appendAndEcho("[kudroid_core] Target Activity: " + targetActivity);
                        if (!fallbackPtrs.empty()) {
                            appendAndEcho("[kudroid_core] Fallback candidates: " +
                                          std::to_string(fallbackPtrs.size()));
                        }
                        if (!manifestComponentFactory.empty()) {
                            appendAndEcho("[kudroid_core] appComponentFactory: " +
                                          manifestComponentFactory);
                        }
                        if (!manifestAppClass.empty()) {
                            appendAndEcho("[kudroid_core] Application class: " + manifestAppClass);
                        }
                        appendAndEcho("[kudroid_core] Launching Android ActivityThread runtime (KuART)...");
                        kudroid::native_phase("activity-thread-before-main");

                        // Register the manifest BEFORE launching. A component reads its
                        // own entry while constructing itself — AGDK's GameActivity asks
                        // getActivityInfo(...).metaData for "android.app.lib_name" inside
                        // onCreate to find the .so holding its renderer — so registering
                        // after launch would be too late and the activity would come up
                        // with no native library and an empty surface.
                        {
                            std::vector<const char*> actPtrs;
                            for (const auto& a : manifestActivities) actPtrs.push_back(a.c_str());
                            kudroid::VFSPathRemapper::getInstance().setPackageName(pkgName);
                            kuart_register_package(pkgName.c_str(),
                                                   actPtrs.empty() ? nullptr : actPtrs.data(),
                                                   static_cast<int>(actPtrs.size()));
                            {
                                int vcode = 1;
                                try {
                                    if (!manifestInfo.versionCode.empty())
                                        vcode = std::stoi(manifestInfo.versionCode);
                                } catch (...) {
                                }
                                const long long nowMs =
                                    std::chrono::duration_cast<std::chrono::milliseconds>(
                                        std::chrono::system_clock::now().time_since_epoch())
                                        .count();
                                kuart_register_package_info(
                                    pkgName.c_str(), vcode,
                                    manifestInfo.versionName.empty()
                                        ? "1.0.0"
                                        : manifestInfo.versionName.c_str(),
                                    nowMs, nowMs);
                            }

                            // Registers keys/values as parallel arrays; the storage has
                            // to outlive the call, hence the vectors of c_str().
                            const auto registerMeta =
                                [&](const std::string& component,
                                    const std::vector<kudroid::MetaDataEntry>& meta) {
                                    if (meta.empty()) return;
                                    std::vector<const char*> keys, values;
                                    keys.reserve(meta.size());
                                    values.reserve(meta.size());
                                    for (const auto& m : meta) {
                                        keys.push_back(m.name.c_str());
                                        values.push_back(m.value.c_str());
                                    }
                                    kuart_register_component_meta_data(
                                        component.c_str(), keys.data(), values.data(),
                                        static_cast<int>(keys.size()));
                                    appendAndEcho("[kudroid_core] meta-data registered for " +
                                                  (component.empty() ? std::string("<application>")
                                                                     : component) +
                                                  ": " + std::to_string(meta.size()) + " entries");
                                    for (const auto& m : meta) {
                                        appendAndEcho("[kudroid_core]     " + m.name + " = " +
                                                      m.value);
                                    }
                                };

                            registerMeta(std::string(), manifestInfo.applicationMetaData);
                            for (const auto& a : manifestInfo.activities) {
                                registerMeta(a.name, a.metaData);
                            }
                        }

                        // Execute JNI_OnLoad for all pre-loaded native libraries so that RegisterNatives
                        // binds all JNI methods (e.g. UnityPlayer native callbacks) before Java startup.
                        for (const auto& [libPath, loader] : manager.libraries()) {
                            invokeJniOnLoad(libPath);
                        }

                        if (!kuart_launch_app(pkgName.c_str(),
                                              manifestComponentFactory.c_str(),
                                              manifestAppClass.c_str(),
                                              targetActivity.c_str(),
                                              fallbackPtrs.empty() ? nullptr : fallbackPtrs.data(),
                                              static_cast<int>(fallbackPtrs.size()))) {
                            appendAndEcho("[kudroid_core] ERROR: ActivityThread.main failed: " +
                                          std::string(kuart_last_error()));
                        }
                        kudroid::native_phase("activity-thread-after-main");
                        // Guest loop exited: drop run-scoped state so the next run
                        // starts clean (counters, stale wait slots, teardown guard).
                        // Safe now — no guest thread is alive to own this state.
                        // Unbind here, not at X: pulling the layer while nativeDone
                        // runs teardown traps the guest on the torn surface.
                        kudroid_unbind_metal_layer();
                        kudroid_gpu_note_run_end();
                        kudroid::blocking_wait_reset_for_test();
                        s_stopping.store(false);
                    }
                }
            }
        }
    }

    const char* trace = kudroid::bionic_shim_trace();
    if (trace && *trace) {
        log += "[kudroid_core] Bionic/global binding trace:\n";
        log += trace;
    }

    writeLogFile("kudroid_run_apk.txt", log);
    s_isApkRunning.store(false);
    char* result = static_cast<char*>(malloc(log.size() + 1));
    if (result) memcpy(result, log.c_str(), log.size() + 1);
    return result;
}
