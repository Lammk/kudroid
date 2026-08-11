#include "kudroid/elf_loader.hpp"
#include "kudroid/BionicShim.h"
#include "kudroid/VFSPathRemapper.h"
#include "kudroid/APKExtractor.h"
#include "kudroid/DexCacheManager.h"
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <csignal>
#include <ctime>
#include <cctype>
#include <filesystem>
#include <string>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ucontext.h>

extern "C" struct JavaVM_* kudroid_jni_get_javavm(void);

// ── ghi nhật ký liên tục vào thư mục có thể ghi của ứng dụng ─────────────────────────
// ứng dụng truyền một thư mục (thư mục documents của nó) thông qua kudroid_set_log_dir().
// nhật ký thành công được ghi dưới dạng tệp .txt; các sự cố (dựa trên tín hiệu, do đó không có ngoại lệ
// c++ nào được kích hoạt) được trình xử lý tín hiệu bắt, bộ đệm nhật ký được đẩy
// vào đĩa chỉ bằng các lệnh gọi an toàn tín hiệu bất đồng bộ trước khi kích hoạt lại.
static char g_logDir[1024] = {0};
const char* g_kudroid_log_dir_ptr = g_logDir;

static char g_crashBuf[16384];
static volatile sig_atomic_t g_crashLen = 0;
static int kudroid_jit_available(void);

static void mirrorCrash(const std::string& log) {
    size_t n = log.size();
    if (n >= sizeof(g_crashBuf)) n = sizeof(g_crashBuf) - 1;
    memcpy(g_crashBuf, log.data(), n);
    g_crashBuf[n] = '\0';
    g_crashLen = (sig_atomic_t)n;
}

static void writeLogFile(const char* name, const std::string& content) {
    if (!g_logDir[0]) return;
    std::string path = std::string(g_logDir) + "/" + name;
    FILE* f = fopen(path.c_str(), "w");
    if (f) {
        fwrite(content.data(), 1, content.size(), f);
        fclose(f);
    }
}

static void appendTestHeader(std::string& log, const char* test, const char* path) {
    auto t = std::time(nullptr);
    auto tm = *std::localtime(&t);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");

    log += "[kudroid_core] ===== " + std::string(test) + " =====\n";
    log += "[kudroid_core] Timestamp: " + oss.str() + "\n";
    if (path && std::strcmp(path, "N/A") != 0) {
        log += "[kudroid_core] Path: " + std::string(path) + "\n";
    }
#if defined(__APPLE__)
    log += "[kudroid_core] JIT: " +
           std::string(kudroid_jit_available() ? "Enabled" : "Disabled") + "\n";
#endif
}

static void crashHandler(int sig, siginfo_t* info, void* ucontext) {
    if (sig == SIGTRAP) {
        if (kudroid::bionic_handle_tpidr_trap(ucontext)) {
            return; // đã xử lý thành công, tiếp tục thực thi!
        }
    }
    
    if (g_logDir[0]) {
        // xây dựng "<dir>/kudroid_crash.log" mà không cấp phát vùng nhớ heap.
        char path[1200];
        size_t dl = strlen(g_logDir);
        if (dl >= sizeof(path) - 32) dl = sizeof(path) - 32;
        memcpy(path, g_logDir, dl);
        const char* suffix = "/kudroid_crash.log";
        memcpy(path + dl, suffix, strlen(suffix) + 1);

        int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) {
            const char* hdr = "[kudroid_core] CRASH — fatal signal caught\n";
            (void)!write(fd, hdr, strlen(hdr));
            char sigline[256];
            int m = snprintf(sigline, sizeof(sigline), "signal = %d\n", sig);
            if (m > 0) (void)!write(fd, sigline, (size_t)m);

            // in địa chỉ lỗi
            if (info) {
                m = snprintf(sigline, sizeof(sigline),
                    "fault_addr = %p\nsi_code = %d\n",
                    info->si_addr, info->si_code);
                if (m > 0) (void)!write(fd, sigline, (size_t)m);
            }

            // in thanh ghi pc (arm64)
#if defined(__aarch64__) || defined(__arm64__)
            if (ucontext) {
#if defined(__APPLE__)
                ucontext_t* uc = (ucontext_t*)ucontext;
                uint64_t pc = uc->uc_mcontext->__ss.__pc;
                uint64_t lr = uc->uc_mcontext->__ss.__lr;
                uint64_t sp = uc->uc_mcontext->__ss.__sp;
                uint64_t x0 = uc->uc_mcontext->__ss.__x[0];
                uint64_t x1 = uc->uc_mcontext->__ss.__x[1];
                m = snprintf(sigline, sizeof(sigline),
                    "pc = 0x%llx\nlr = 0x%llx\nsp = 0x%llx\n"
                    "x0 = 0x%llx\nx1 = 0x%llx\n",
                    (unsigned long long)pc, (unsigned long long)lr,
                    (unsigned long long)sp,
                    (unsigned long long)x0, (unsigned long long)x1);
                if (m > 0) (void)!write(fd, sigline, (size_t)m);
#elif defined(__linux__)
                ucontext_t* uc = (ucontext_t*)ucontext;
                uint64_t pc = uc->uc_mcontext.pc;
                uint64_t lr = uc->uc_mcontext.regs[30];
                m = snprintf(sigline, sizeof(sigline),
                    "pc = 0x%llx\nlr = 0x%llx\n",
                    (unsigned long long)pc, (unsigned long long)lr);
                if (m > 0) (void)!write(fd, sigline, (size_t)m);
#endif
            }
#endif

            (void)!write(fd, "--- log up to crash ---\n", 24);
            (void)!write(fd, g_crashBuf, (size_t)g_crashLen);
            
            const char* traceStr = kudroid::bionic_shim_trace();
            if (traceStr && *traceStr) {
                (void)!write(fd, "\n--- bionic shim trace ---\n", 27);
                (void)!write(fd, traceStr, strlen(traceStr));
            }
            
            close(fd);
        }
    }
    signal(sig, SIG_DFL);
    raise(sig);
}

static void installCrashHandlers(void) {
    static bool installed = false;
    if (installed) return;
    installed = true;
    
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = crashHandler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    
    sigaction(SIGILL,  &sa, nullptr);
    sigaction(SIGBUS,  &sa, nullptr);
    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGTRAP, &sa, nullptr);
    sigaction(SIGABRT, &sa, nullptr);
}

extern "C" void kudroid_set_log_dir(const char* dir) {
    if (!dir) return;
    strncpy(g_logDir, dir, sizeof(g_logDir) - 1);
    g_logDir[sizeof(g_logDir) - 1] = '\0';
    installCrashHandlers();
}

extern "C" void kudroid_set_documents_dir(const char* dir) {
    if (dir) kudroid::VFSPathRemapper::getInstance().setDocumentsDirectory(dir);
}

// con trỏ lớp metal toàn cục có thể truy cập bằng bionicshim
void* g_metalLayer = nullptr;
int g_metalLayerWidth = 1080;
int g_metalLayerHeight = 1920;

extern "C" void kudroid_set_metal_layer(void* layer, int width, int height) {
    g_metalLayer = layer;
    g_metalLayerWidth = width;
    g_metalLayerHeight = height;
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

extern "C" const char* kudroid_install_apk(const char* apkPath) {
    std::string log = "[kudroid_core] ===== APK Install =====\n";
    if (!apkPath || !*apkPath) {
        log += "[kudroid_apk] ERROR: APK path is empty\n";
    } else {
        const std::filesystem::path source(apkPath);
        std::string appName = source.stem().string();
        if (appName.empty()) appName = "unnamed_apk";
        for (char& character : appName) {
            if (!(std::isalnum(static_cast<unsigned char>(character)) || character == '_' || character == '-')) {
                character = '_';
            }
        }
        auto& remapper = kudroid::VFSPathRemapper::getInstance();
        const std::filesystem::path target = std::filesystem::path(remapper.androidRoot()) /
                                             "data/app" / appName;
        log += "[kudroid_apk] APK: " + source.string() + "\n";
        log += "[kudroid_apk] Target extraction directory: " + target.string() + "\n";
        if (kudroid::APKExtractor::extract_apk(source.string(), target.string())) {
            log += "[kudroid_apk] APK extracted successfully\n";
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
#include <TargetConditionals.h>

// csops() là một api riêng tư nhưng ổn định; được sử dụng để đọc trạng thái chữ ký mã của quá trình.
// cs_debugged được đặt khi đường dẫn jit (ký mã động) hoạt động
// dưới livecontainer / trình gỡ lỗi, điều này cho phép các trang prot_exec chạy.
extern "C" int csops(pid_t pid, unsigned int ops, void* useraddr, size_t usersize);
#ifndef CS_OPS_STATUS
#define CS_OPS_STATUS 0
#endif
#ifndef CS_DEBUGGED
#define CS_DEBUGGED 0x10000000
#endif

// trả về 1 nếu jit (bộ nhớ có thể thực thi) có vẻ khả dụng, ngược lại trả về 0.
static int kudroid_jit_available(void) {
    unsigned int flags = 0;
    // cs_debugged là tín hiệu đáng tin cậy duy nhất trên ios: nó được đặt khi trình gỡ lỗi
    // (livecontainer/debugserver) đã bật ký mã động, điều này
    // chính xác cho phép thực thi các trang prot_exec. thăm dò rwx mmap không
    // đáng tin cậy — lời gọi hệ thống thành công mà không cần jit, nhưng quá trình thực thi vẫn bị lỗi,
    // đưa ra kết quả sai "enabled".
    if (csops(getpid(), CS_OPS_STATUS, &flags, sizeof(flags)) != 0) {
        return 0;
    }
    return (flags & CS_DEBUGGED) ? 1 : 0;
}
#else
static int kudroid_jit_available(void) { return 1; }
#endif

extern "C" const char* kudroid_jit_status(void) {
    const char* text = kudroid_jit_available()
        ? "JIT: Enabled"
        : "JIT: Disabled";
    char* result = (char*)malloc(strlen(text) + 1);
    if (result) memcpy(result, text, strlen(text) + 1);
    return result;
}

#include "kudroid/kudroid_jni.h"

// --- định nghĩa nativeactivity ---

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


// khai báo setter bên ngoài
extern "C" void kudroid_jni_set_log_callback(void (*cb)(const char*));

extern "C" char* kudroid_test_jvm(const char* rt_jar_path) {
    std::string log;
    appendTestHeader(log, "JVM Integration Test", "N/A");
    installCrashHandlers();
    
    log += "[kudroid_core] Phase: init JVM via JNI Bridge\n";
    kudroid::bionic_shim_reset_trace();

    // biến toàn cục để giữ tham chiếu nhật ký cho lệnh gọi lại c
    static std::string* g_jvm_test_log = &log;
    g_jvm_test_log = &log;
    
    kudroid_jni_set_log_callback([](const char* msg) {
        if (g_jvm_test_log) {
            *g_jvm_test_log += "[kudroid_jni] ";
            *g_jvm_test_log += msg;
            *g_jvm_test_log += "\n";
        }
    });

    kudroid_jni_init_jvm(rt_jar_path ? rt_jar_path : "", "");
    
    JavaVM* vm = kudroid_jni_get_javavm();
    if (!vm) {
        log += "[kudroid_core] ERROR: JavaVM is null!\n";
        return strdup(log.c_str());
    }
    
    JNIEnv* env = nullptr;
    kudroid_jni_get_env(vm, reinterpret_cast<void**>(&env), 0);
    
    if (env) {
        log += "[kudroid_core] Phase: testing JNI FindClass\n";
        jclass strClass = env->functions->FindClass(env, "java/lang/String");
        if (strClass) {
            log += "[kudroid_core] SUCCESS: Found java/lang/String class via JNI!\n";
        } else {
            log += "[kudroid_core] WARNING: java/lang/String class not found (expected if rt.jar is missing)\n";
        }
        
        jstring testStr = env->functions->NewStringUTF(env, "Hello JNI");
        if (testStr) {
            const char* utf = env->functions->GetStringUTFChars(env, testStr, nullptr);
            log += "[kudroid_core] SUCCESS: Created JNI string: ";
            log += utf ? utf : "null";
            log += "\n";
            env->functions->ReleaseStringUTFChars(env, testStr, utf);
        }
    } else {
        log += "[kudroid_core] ERROR: Failed to get JNIEnv!\n";
    }
    
    log += "[kudroid_core] Phase: destroy JVM\n";
    kudroid_jni_destroy_jvm();
    
    kudroid_jni_set_log_callback(nullptr);
    g_jvm_test_log = nullptr;

    log += "[kudroid_core] JVM test completed.\n";
    
    return strdup(log.c_str());
}

extern "C" const char* kudroid_run_apk(const char* appName) {
    std::string log;
    appendTestHeader(log, "Run APK Native Libraries", appName);
    kudroid::bionic_shim_reset_trace();
    log += "[kudroid_core] Phase: init LibraryManager\n";

    if (!appName || !*appName) {
        log += "[kudroid_core] ERROR: null or empty app name\n";
    } else {
        auto& remapper = kudroid::VFSPathRemapper::getInstance();
        const std::filesystem::path libDir = std::filesystem::path(remapper.androidRoot()) /
                                             "data/app" / appName / "lib/arm64-v8a";
        
        log += "[kudroid_core] Scanning library directory: " + libDir.string() + "\n";
        
        if (!std::filesystem::exists(libDir)) {
            log += "[kudroid_core] ERROR: Library directory does not exist. Did you install the APK?\n";
        } else {
            kudroid::LibraryManager manager;
            
            // thu thập tất cả các tệp .so để tải
            std::vector<std::string> soFiles;
            for (const auto& entry : std::filesystem::directory_iterator(libDir)) {
                if (entry.path().extension() == ".so") {
                    soFiles.push_back(entry.path().string());
                }
            }
            
            if (soFiles.empty()) {
                log += "[kudroid_core] WARNING: No .so files found in the APK library directory.\n";
            } else {
                for (const auto& soPath : soFiles) {
                    log += "[kudroid_core] Attempting to load: " + soPath + "\n";
                    if (!manager.loadRecursive(soPath.c_str())) {
                        log += "[kudroid_core] LOAD FAILED for " + soPath + ": " + manager.lastError() + "\n";
                    } else {
                        log += "[kudroid_core] LOAD SUCCESS for " + soPath + "\n";
                    }
                }
                
                log += "[kudroid_core] Total loaded libraries (including dependencies): " + std::to_string(manager.libraries().size()) + "\n";
                log += "[kudroid_core] Native libraries loaded into memory successfully!\n";
                
                log += "[kudroid_core] --- Memory Map ---\n";
                for (const auto& pair : manager.libraries()) {
                    char mapLine[256];
                    snprintf(mapLine, sizeof(mapLine), "  %s -> %p\n", pair.first.c_str(), pair.second->baseAddress());
                    log += mapLine;
                }
                log += "[kudroid_core] ------------------\n";
                
                mirrorCrash(log);

                // đảm bảo jvm avian được khởi tạo trước khi gọi jni_onload.
                // tệp boot jar (các lớp khung) được nhúng trong tệp nhị phân.
                kudroid_jni_init_jvm("", "");
                JavaVM* jvm = kudroid_jni_get_javavm();
                if (!jvm) {
                    log += "[kudroid_core] ERROR: Avian JVM failed to initialize. "
                           "JNI_OnLoad will not be invoked.\n";
                    mirrorCrash(log);
                } else {
                    char jvmLine[128];
                    snprintf(jvmLine, sizeof(jvmLine),
                             "[kudroid_core] Avian JVM ready (JavaVM=%p).\n", (void*)jvm);
                    log += jvmLine;

                    auto jni_onload = reinterpret_cast<jint (*)(JavaVM*, void*)>(
                        manager.resolveAppSymbol("JNI_OnLoad")
                    );
                    if (jni_onload) {
                        log += "[kudroid_core] Found JNI_OnLoad, invoking...\n";
                        mirrorCrash(log);

                        bionic_init_main_thread_tls();

                        jint version = jni_onload(jvm, nullptr);
                        log += "[kudroid_core] JNI_OnLoad returned version: " + std::to_string(version) + "\n";
                        mirrorCrash(log);
                    } else {
                        log += "[kudroid_core] JNI_OnLoad not found.\n";
                        mirrorCrash(log);
                    }

                    auto native_activity_create = reinterpret_cast<void (*)(ANativeActivity*, void*, size_t)>(
                        manager.resolveAppSymbol("ANativeActivity_onCreate")
                    );
                    if (native_activity_create) {
                        log += "[kudroid_core] Found ANativeActivity_onCreate, invoking...\n";
                        mirrorCrash(log);

                        // thiết lập các lệnh gọi lại hoạt động để trò chơi có thể bắt đầu
                        // kết xuất và nhận đầu vào. đây là các hook
                        // mà trò chơi đăng ký các trình xử lý.
                        static ANativeActivityCallbacks mock_callbacks = {};
                        static ANativeActivity mock_activity = {
                            &mock_callbacks,
                            jvm,
                            nullptr, // env
                            nullptr, // clazz
                            "/sdcard/Android/data/test", // internalDataPath
                            "/sdcard/Android/data/test", // externalDataPath
                            29, // sdkVersion
                            nullptr, // instance
                            nullptr, // assetManager
                            nullptr  // obbPath
                        };
                        kudroid_jni_get_env(jvm, reinterpret_cast<void**>(&mock_activity.env), 0);

                        // gọi oncreate. trò chơi lưu trữ trạng thái của nó trong
                        // activity->instance và đăng ký các lệnh gọi lại của nó.
                        native_activity_create(&mock_activity, nullptr, 0);
                        log += "[kudroid_core] ANativeActivity_onCreate completed.\n";
                        mirrorCrash(log);

                        // điều khiển vòng đời để trò chơi bắt đầu kết xuất:
                        // onstart -> onresume -> onwindowfocuschanged(true)
                        // -> onnativewindowcreated -> oninputqueuecreated.
                        auto call = [&](const char* name, void* fn) {
                            if (fn) {
                                log += std::string("[kudroid_core] Calling ") + name + "\n";
                                mirrorCrash(log);
                                // tất cả các lệnh gọi lại đều nhận (anativeactivity*, ...).
                                reinterpret_cast<void (*)(ANativeActivity*)>(fn)(&mock_activity);
                            }
                        };
                        call("onStart", mock_callbacks.onStart);
                        call("onResume", mock_callbacks.onResume);
                        call("onWindowFocusChanged", mock_callbacks.onWindowFocusChanged);
                        call("onNativeWindowCreated", mock_callbacks.onNativeWindowCreated);
                        call("onInputQueueCreated", mock_callbacks.onInputQueueCreated);
                        log += "[kudroid_core] Lifecycle callbacks invoked.\n";
                        mirrorCrash(log);
                    } else {
                        log += "[kudroid_core] ANativeActivity_onCreate not found.\n";
                        mirrorCrash(log);
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
    char* result = static_cast<char*>(malloc(log.size() + 1));
    if (result) memcpy(result, log.c_str(), log.size() + 1);
    return result;
}

extern "C" int kudroid_self_test(void) {
    fprintf(stderr, "[kudroid_core] Self-test starting...\n");
    fprintf(stderr, "[kudroid_core] Creating ElfLoader with dummy path '/nonexistent'...\n");

    try {
        kudroid::ElfLoader loader("/nonexistent");

        fprintf(stderr, "[kudroid_core] ElfLoader constructed OK.\n");
        fprintf(stderr, "[kudroid_core] isLoaded() = %s\n", loader.isLoaded() ? "true" : "false");
        fprintf(stderr, "[kudroid_core] entryPoint() = 0x%llx\n", (unsigned long long)loader.entryPoint());
        fprintf(stderr, "[kudroid_core] segments count = %zu\n", loader.segments().size());

        fprintf(stderr, "[kudroid_core] Calling parse()...\n");
        bool ok = loader.parse();
        fprintf(stderr, "[kudroid_core] parse() returned %s\n", ok ? "true" : "false");

        fprintf(stderr, "[kudroid_core] Self-test PASSED.\n");
        (void)loader;
        return 0;
    } catch (...) {
        fprintf(stderr, "[kudroid_core] Self-test FAILED: exception thrown!\n");
        return -1;
    }
}

extern "C" const char* kudroid_self_test_log(void) {
    std::string log;
    appendTestHeader(log, "Self-Test", "/nonexistent");
    log += "[kudroid_core] Phase: construct ElfLoader\n";
    log += "[kudroid_core] Creating ElfLoader with dummy path '/nonexistent'...\n";

    try {
        kudroid::ElfLoader loader("/nonexistent");

        log += "[kudroid_core] ElfLoader constructed OK.\n";

        char buf[256];
        snprintf(buf, sizeof(buf), "[kudroid_core] isLoaded() = %s\n", loader.isLoaded() ? "true" : "false");
        log += buf;

        snprintf(buf, sizeof(buf), "[kudroid_core] entryPoint() = 0x%llx\n", (unsigned long long)loader.entryPoint());
        log += buf;

        snprintf(buf, sizeof(buf), "[kudroid_core] segments count = %zu\n", loader.segments().size());
        log += buf;

        log += "[kudroid_core] Phase: parse()\n";
        bool ok = loader.parse();

        snprintf(buf, sizeof(buf), "[kudroid_core] parse() returned %s\n", ok ? "true" : "false");
        log += buf;

        log += "[kudroid_core] Self-test PASSED.\n";
        (void)loader;

        writeLogFile("kudroid_selftest.txt", log);
        char* result = (char*)malloc(log.size() + 1);
        if (result) {
            memcpy(result, log.c_str(), log.size() + 1);
        }
        return result;
    } catch (...) {
        log += "[kudroid_core] Self-test FAILED: exception thrown!\n";

        writeLogFile("kudroid_selftest.txt", log);
        char* result = (char*)malloc(log.size() + 1);
        if (result) {
            memcpy(result, log.c_str(), log.size() + 1);
        }
        return result;
    }
}

extern "C" const char* kudroid_load_elf(const char* path) {
    std::string log;
    appendTestHeader(log, "ELF Load", path);
    if (!path) {
        log += "[kudroid_core] ERROR: null path\n";
        char* result = (char*)malloc(log.size() + 1);
        if (result) memcpy(result, log.c_str(), log.size() + 1);
        return result;
    }

    char buf[512];
    snprintf(buf, sizeof(buf), "[kudroid_core] Loading ELF: %s\n", path);
    log += buf;

    try {
        kudroid::ElfLoader loader(path);

        log += "[kudroid_core] Phase: parse ELF headers\n";
        bool ok = loader.parse();

        if (!ok) {
            snprintf(buf, sizeof(buf), "[kudroid_core] PARSE FAILED: %s\n", loader.lastError());
            log += buf;
        } else {
            log += "[kudroid_core] ELF parsed successfully.\n";

            snprintf(buf, sizeof(buf), "[kudroid_core]   Entry point: 0x%llx\n", (unsigned long long)loader.entryPoint());
            log += buf;

            snprintf(buf, sizeof(buf), "[kudroid_core]   PT_LOAD segments: %zu\n", loader.segments().size());
            log += buf;

            int segNum = 0;
            for (const auto& seg : loader.segments()) {
                snprintf(buf, sizeof(buf),
                    "[kudroid_core]   [%d] vaddr=0x%llx offset=0x%llx filesz=%llu memsz=%llu flags=0x%x\n",
                    segNum++,
                    (unsigned long long)seg.vaddr,
                    (unsigned long long)seg.offset,
                    (unsigned long long)seg.filesz,
                    (unsigned long long)seg.memsz,
                    seg.flags);
                log += buf;
            }

            // thử ánh xạ (hiện tại là hàm giả)
            log += "[kudroid_core] Phase: map PT_LOAD segments\n";
            if (loader.map()) {
                log += "[kudroid_core] Map OK.\n";
            } else {
                snprintf(buf, sizeof(buf), "[kudroid_core] Map failed: %s\n", loader.lastError());
                log += buf;
            }

            // thử định vị lại (hiện tại là hàm giả)
            log += "[kudroid_core] Phase: resolve relocations/imports\n";
            if (loader.relocate()) {
                log += "[kudroid_core] Relocate OK.\n";
            } else {
                snprintf(buf, sizeof(buf), "[kudroid_core] Relocate failed: %s\n", loader.lastError());
                log += buf;
            }
        }

        log += "[kudroid_core] Load complete.\n";

        writeLogFile("kudroid_load.txt", log);
        char* result = (char*)malloc(log.size() + 1);
        if (result) memcpy(result, log.c_str(), log.size() + 1);
        return result;
    } catch (...) {
        log += "[kudroid_core] EXCEPTION during load!\n";
        writeLogFile("kudroid_load.txt", log);
        char* result = (char*)malloc(log.size() + 1);
        if (result) memcpy(result, log.c_str(), log.size() + 1);
        return result;
    }
}

extern "C" const char* kudroid_execution_test(const char* path) {
    std::string log;
    appendTestHeader(log, "ELF Execution", path);
    if (!path) {
        log += "[kudroid_core] ERROR: null path\n";
        char* result = (char*)malloc(log.size() + 1);
        if (result) memcpy(result, log.c_str(), log.size() + 1);
        return result;
    }

    char buf[512];
    snprintf(buf, sizeof(buf), "[kudroid_core] Execution test for: %s\n", path);
    log += buf;

    // từ chối chạy mã gốc khi jit bị tắt: việc thực thi các trang prot_exec
    // mà không có ký mã động sẽ làm lỗi toàn bộ quá trình và nhật ký
    // được đệm bên dưới sẽ không bao giờ được trả về. hãy thất bại lớn tiếng thay vì gặp sự cố.
    if (!kudroid_jit_available()) {
        log += "[kudroid_core] ABORT: JIT is Disabled — cannot execute native code.\n";
        log += "[kudroid_core] Enable JIT in LiveContainer and retry.\n";
        char* result = (char*)malloc(log.size() + 1);
        if (result) memcpy(result, log.c_str(), log.size() + 1);
        return result;
    }

    try {
        kudroid::ElfLoader loader(path);

        if (!loader.parse()) {
            snprintf(buf, sizeof(buf), "[kudroid_core] PARSE FAILED: %s\n", loader.lastError());
            log += buf;
            writeLogFile("kudroid_exec.txt", log);
            char* result = (char*)malloc(log.size() + 1);
            if (result) memcpy(result, log.c_str(), log.size() + 1);
            return result;
        }

        log += "[kudroid_core] Phase: parse -> OK\n";

        if (!loader.map()) {
            snprintf(buf, sizeof(buf), "[kudroid_core] MAP FAILED: %s\n", loader.lastError());
            log += buf;
            writeLogFile("kudroid_exec.txt", log);
            char* result = (char*)malloc(log.size() + 1);
            if (result) memcpy(result, log.c_str(), log.size() + 1);
            return result;
        }

        log += "[kudroid_core] Phase: map -> OK\n";

        if (!loader.relocate()) {
            snprintf(buf, sizeof(buf), "[kudroid_core] RELOCATE FAILED: %s\n", loader.lastError());
            log += buf;
            writeLogFile("kudroid_exec.txt", log);
            char* result = (char*)malloc(log.size() + 1);
            if (result) memcpy(result, log.c_str(), log.size() + 1);
            return result;
        }

        log += "[kudroid_core] Phase: relocate/import binding -> OK\n";
        log += "[kudroid_core] Phase: invoke exported kudroid_add(40, 20)\n";

        // chụp nhanh nhật ký cho trình xử lý sự cố: lời gọi bên dưới nhảy vào
        // mã đã được jit và có thể bị lỗi (tín hiệu, không phải ngoại lệ). nếu có,
        // trình xử lý sẽ đẩy bộ đệm này vào kudroid_crash.log.
        mirrorCrash(log);

        std::string execResult = loader.testExecution();
        log += execResult;
        log += "\n";

        writeLogFile("kudroid_exec.txt", log);
        char* result = (char*)malloc(log.size() + 1);
        if (result) memcpy(result, log.c_str(), log.size() + 1);
        return result;
    } catch (...) {
        log += "[kudroid_core] EXCEPTION during execution test!\n";
        writeLogFile("kudroid_exec.txt", log);
        char* result = (char*)malloc(log.size() + 1);
        if (result) memcpy(result, log.c_str(), log.size() + 1);
        return result;
    }
}

extern "C" const char* kudroid_bionic_execution_test(const char* path) {
    std::string log;
    appendTestHeader(log, "Bionic Shim Execution", path);
    kudroid::bionic_shim_reset_trace();
    if (!path) {
        log += "[kudroid_core] ERROR: null path\n";
    } else if (!kudroid_jit_available()) {
        log += "[kudroid_core] ABORT: JIT is Disabled\n";
    } else {
        kudroid::ElfLoader loader(path);
        if (!loader.parse()) {
            log += "[kudroid_core] PARSE FAILED: " + std::string(loader.lastError()) + "\n";
        } else if (!loader.map()) {
            log += "[kudroid_core] MAP FAILED: " + std::string(loader.lastError()) + "\n";
        } else if (!loader.relocate()) {
            log += "[kudroid_core] RELOCATE FAILED: " + std::string(loader.lastError()) + "\n";
        } else {
            log += "[kudroid_core] ELF mapped and Bionic imports bound.\n";
            void* address = loader.getSymbolAddress("kudroid_bionic_test");
            if (!address) {
                log += "[kudroid_core] SYMBOL FAILED: kudroid_bionic_test not found\n";
            } else {
                log += "[kudroid_core] Running kudroid_bionic_test()...\n";
                const char* shimTrace = kudroid::bionic_shim_trace();
                if (shimTrace && *shimTrace) {
                    log += "[kudroid_core] Bionic trace before call:\n";
                    log += shimTrace;
                }
                mirrorCrash(log);
                using TestFunction = int (*)();
                const int result = reinterpret_cast<TestFunction>(address)();
                log += "[kudroid_core] BIONIC TEST RESULT: " +
                       std::to_string(result) + (result == 0 ? " (SUCCESS)\n" : " (FAILED)\n");
            }
        }
    }

    const char* shimTrace = kudroid::bionic_shim_trace();
    if (shimTrace && *shimTrace) {
        log += "[kudroid_core] Bionic trace:\n";
        log += shimTrace;
    }

    writeLogFile("kudroid_bionic_test.txt", log);
    char* result = static_cast<char*>(malloc(log.size() + 1));
    if (result) memcpy(result, log.c_str(), log.size() + 1);
    return result;
}

extern "C" const char* kudroid_multi_elf_test(const char* consumerPath,
                                               const char* providerPath) {
    std::string log;
    appendTestHeader(log, "Multi-ELF Dependency Resolution", consumerPath);
    kudroid::bionic_shim_reset_trace();
    log += "[kudroid_core] Phase: create LibraryManager\n";

    if (!consumerPath || !providerPath) {
        log += "[kudroid_core] ERROR: null ELF path\n";
    } else {
        kudroid::LibraryManager manager;
        log += "[kudroid_core] Phase: parse DT_NEEDED\n";
        const auto dependencies = kudroid::parse_elf_dependencies(consumerPath);
        log += "[kudroid_core] DT_NEEDED count: " +
               std::to_string(dependencies.size()) + "\n";
        for (const auto& dependency : dependencies) {
            log += "[kudroid_core]   dependency: " + dependency + "\n";
        }

        log += "[kudroid_core] Phase: load primary ELF recursively\n";
        if (!manager.loadRecursive(consumerPath)) {
            log += "[kudroid_core] PRIMARY LOAD FAILED: " +
                   manager.lastError() + "\n";
        } else {
            log += "[kudroid_core] Primary ELF load OK\n";
        }

        log += "[kudroid_core] Phase: load sibling ELF recursively\n";
        if (!manager.loadRecursive(providerPath)) {
            log += "[kudroid_core] SIBLING LOAD FAILED: " +
                   manager.lastError() + "\n";
        } else {
            log += "[kudroid_core] Sibling ELF load OK\n";
        }

        const std::size_t loadedCount = manager.libraries().size();
        log += "[kudroid_core] Loaded library count: " +
               std::to_string(loadedCount) + "\n";
        for (const auto& library : manager.libraries()) {
            log += "[kudroid_core]   loaded: " + library.first + "\n";
        }

        log += "[kudroid_core] Phase: duplicate-load prevention\n";
        const bool duplicateLoad = manager.loadRecursive(consumerPath);
        log += duplicateLoad && manager.libraries().size() == loadedCount
            ? "[kudroid_core] Duplicate load skipped successfully\n"
            : "[kudroid_core] Duplicate load check failed\n";

        log += "[kudroid_core] Phase: global symbol resolution\n";
        void* symbol = manager.resolveGlobalSymbol("kudroid_dependency_value");
        log += symbol
            ? "[kudroid_core] Global symbol kudroid_dependency_value resolved from provider\n"
            : "[kudroid_core] Global symbol kudroid_dependency_value NOT resolved\n";

        log += "[kudroid_core] Phase: execute consumer -> provider call\n";
        void* consumerSymbol = manager.resolveGlobalSymbol("kudroid_multi_elf_test");
        if (!consumerSymbol) {
            log += "[kudroid_core] Consumer symbol kudroid_multi_elf_test NOT resolved\n";
        } else if (!kudroid_jit_available()) {
            log += "[kudroid_core] Consumer execution skipped: JIT Disabled\n";
        } else {
            const char* trace = kudroid::bionic_shim_trace();
            if (trace && *trace) {
                log += "[kudroid_core] Import trace before execution:\n";
                log += trace;
            }
            mirrorCrash(log);
            using MultiElfFunction = int (*)();
            const int result = reinterpret_cast<MultiElfFunction>(consumerSymbol)();
            log += "[kudroid_core] Consumer returned: " + std::to_string(result) + "\n";
            log += result == 42
                ? "[kudroid_core] MULTI-ELF TEST RESULT: SUCCESS (35 + 7 = 42)\n"
                : "[kudroid_core] MULTI-ELF TEST RESULT: FAILED (expected 42)\n";
        }
    }

    const char* trace = kudroid::bionic_shim_trace();
    if (trace && *trace) {
        log += "[kudroid_core] Bionic/global binding trace:\n";
        log += trace;
    }

    writeLogFile("kudroid_multi_elf_test.txt", log);
    char* result = static_cast<char*>(malloc(log.size() + 1));
    if (result) memcpy(result, log.c_str(), log.size() + 1);
    return result;
}

extern "C" int kudroid_clear_app_cache(const char* package_name) {
    if (!package_name) return 0;
    
    // xây dựng đường dẫn đến thư mục bộ đệm của ứng dụng bằng logic ánh xạ vfs
    const std::string androidRoot = kudroid::VFSPathRemapper::getInstance().androidRoot();
    std::filesystem::path cachePath = std::filesystem::path(androidRoot) / "data/data" / package_name / "cache";
    std::filesystem::path codeCachePath = std::filesystem::path(androidRoot) / "data/data" / package_name / "code_cache";
    
    std::error_code ec;
    int success = 1;
    if (std::filesystem::exists(cachePath, ec)) {
        if (std::filesystem::remove_all(cachePath, ec) == static_cast<std::uintmax_t>(-1)) {
            success = 0;
        }
    }
    if (std::filesystem::exists(codeCachePath, ec)) {
        if (std::filesystem::remove_all(codeCachePath, ec) == static_cast<std::uintmax_t>(-1)) {
            success = 0;
        }
    }
    return success;
}

extern "C" int kudroid_delete_app(const char* package_name) {
    if (!package_name) return 0;
    
    const std::string androidRoot = kudroid::VFSPathRemapper::getInstance().androidRoot();
    std::filesystem::path appCodePath = std::filesystem::path(androidRoot) / "data/app" / package_name;
    std::filesystem::path appDataPath = std::filesystem::path(androidRoot) / "data/data" / package_name;
    
    std::error_code ec;
    int success = 1;
    if (std::filesystem::exists(appCodePath, ec)) {
        if (std::filesystem::remove_all(appCodePath, ec) == static_cast<std::uintmax_t>(-1)) {
            success = 0;
        }
    }
    if (std::filesystem::exists(appDataPath, ec)) {
        if (std::filesystem::remove_all(appDataPath, ec) == static_cast<std::uintmax_t>(-1)) {
            success = 0;
        }
    }
    return success;
}

extern "C" const char* kudroid_get_app_info(const char* package_name) {
    if (!package_name) return strdup("{}");
    
    const std::string androidRoot = kudroid::VFSPathRemapper::getInstance().androidRoot();
    std::filesystem::path appDir = std::filesystem::path(androidRoot) / "data/data" / package_name;
    
    uintmax_t totalSize = 0;
    std::error_code ec;
    
    if (std::filesystem::exists(appDir, ec)) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(appDir, ec)) {
            if (!std::filesystem::is_directory(entry.status(ec))) {
                totalSize += std::filesystem::file_size(entry, ec);
            }
        }
    }
    
    char buf[512];
    std::snprintf(buf, sizeof(buf), 
        "{\n"
        "  \"package_name\": \"%s\",\n"
        "  \"data_size_bytes\": %llu,\n"
        "  \"installed\": %s\n"
        "}", 
        package_name, 
        static_cast<unsigned long long>(totalSize),
        std::filesystem::exists(appDir, ec) ? "true" : "false");
        
    return strdup(buf);
}

// ─────────────────────────────────────────────────────────────────────────────
extern "C" const char* kudroid_syscall_so_test(const char* path) {
    std::string log;
    appendTestHeader(log, "Syscall Traps (ARM64 .so) Test", path);
    if (!path) {
        log += "[kudroid_syscall] ERROR: null path\n";
    } else if (!kudroid_jit_available()) {
        log += "[kudroid_syscall] ABORT: JIT is Disabled — cannot execute ARM64 ELF\n";
    } else {
        kudroid::ElfLoader loader(path);
        if (!loader.parse()) {
            log += "[kudroid_syscall] PARSE FAILED: " + std::string(loader.lastError()) + "\n";
        } else if (!loader.map()) {
            log += "[kudroid_syscall] MAP FAILED: " + std::string(loader.lastError()) + "\n";
        } else if (!loader.relocate()) {
            log += "[kudroid_syscall] RELOCATE FAILED: " + std::string(loader.lastError()) + "\n";
        } else {
            log += "[kudroid_syscall] ELF mapped and Bionic imports bound.\n";
            void* address = loader.getSymbolAddress("kudroid_syscall_test");
            if (!address) {
                log += "[kudroid_syscall] SYMBOL FAILED: kudroid_syscall_test not found\n";
            } else {
                log += "[kudroid_syscall] Running kudroid_syscall_test()...\n";
                int (*test_func)() = reinterpret_cast<int (*)()>(address);
                
                // xóa bộ đệm theo dõi bionicshim
                kudroid::bionic_shim_reset_trace();
                
                int result = test_func();
                log += "[kudroid_syscall] SYSCALL TEST RESULT: " +
                       (result == 0 ? std::string("0 (SUCCESS)") : std::to_string(result) + " (FAILED)") + "\n";
            }
        }
        log += "[kudroid_syscall] Bionic shim trace:\n";
        log += "[BionicShim] ";
        log += kudroid::bionic_shim_trace();
        log += "\n";
    }
    writeLogFile("kudroid_syscall_test.txt", log);
    return strdup(log.c_str());
}

extern "C" const char* kudroid_jni_massive_so_test(const char* path) {
    std::string log;
    appendTestHeader(log, "Massive JNI 200+ Functions Test", path);
    if (!path) {
        log += "[kudroid_jni] ERROR: null path\n";
    } else if (!kudroid_jit_available()) {
        log += "[kudroid_jni] ABORT: JIT is Disabled — cannot execute ARM64 ELF\n";
    } else {
        kudroid::ElfLoader loader(path);
        if (!loader.parse()) {
            log += "[kudroid_jni] PARSE FAILED: " + std::string(loader.lastError()) + "\n";
        } else if (!loader.map()) {
            log += "[kudroid_jni] MAP FAILED: " + std::string(loader.lastError()) + "\n";
        } else if (!loader.relocate()) {
            log += "[kudroid_jni] RELOCATE FAILED: " + std::string(loader.lastError()) + "\n";
        } else {
            loader.registerEhFrame();
            loader.executeInit();
            log += "[kudroid_jni] ELF mapped, relocated, and initialized.\n";
            
            // thực thi jni_onload nếu có
            void* jniOnLoadAddr = loader.getSymbolAddress("JNI_OnLoad");
            if (jniOnLoadAddr) {
                log += "[kudroid_jni] Found JNI_OnLoad, executing...\n";
                // lấy jvm từ bộ nối
                using JNI_OnLoad_t = jint (*)(JavaVM*, void*);
                JNI_OnLoad_t jniOnLoad = reinterpret_cast<JNI_OnLoad_t>(jniOnLoadAddr);
                jint version = jniOnLoad(kudroid_jni_get_javavm(), nullptr);
                log += "[kudroid_jni] JNI_OnLoad returned version: 0x" + std::to_string(version) + "\n";
            }
            
            void* address = loader.getSymbolAddress("kudroid_jni_massive_test");
            if (!address) {
                log += "[kudroid_jni] SYMBOL FAILED: kudroid_jni_massive_test not found\n";
            } else {
                log += "[kudroid_jni] Running kudroid_jni_massive_test()...\n";
                int (*test_func)(void*) = reinterpret_cast<int (*)(void*)>(address);
                
                // thiết lập lệnh gọi lại
                static std::string* g_jni_test_log = &log;
                g_jni_test_log = &log;
                kudroid_jni_set_log_callback([](const char* msg) {
                    if (g_jni_test_log) {
                        *g_jni_test_log += "[kudroid_jni] ";
                        *g_jni_test_log += msg;
                        *g_jni_test_log += "\n";
                    }
                });

                kudroid_jni_init_jvm("", ""); // đảm bảo nó được khởi tạo
                void* vm = kudroid_jni_get_javavm();
                
                int result = test_func(vm);
                log += "[kudroid_jni] TEST RESULT: " +
                       (result == 0 ? std::string("0 (SUCCESS)") : std::to_string(result) + " (FAILED)") + "\n";
            }
        }
    }
    writeLogFile("kudroid_jni_massive_test.txt", log);
    return strdup(log.c_str());
}

// các bài kiểm tra thực thi gpu .so — tải tệp .so kiểm tra elf arm64 thông qua trình
// tải elf, trình này gọi dlopen/dlsym cho các thư viện gpu. bionicshim chặn các lời gọi đó
// và ánh xạ chúng trực tiếp tới gốc ios (moltenvk / angle).
// ─────────────────────────────────────────────────────────────────────────────

extern "C" const char* kudroid_gpu_vulkan_so_test(const char* path) {
    std::string log;
    appendTestHeader(log, "GPU Vulkan .so Intercept Test", path);
    kudroid::bionic_shim_reset_trace();
    if (!path) {
        log += "[kudroid_gpu] ERROR: null path\n";
    } else if (!kudroid_jit_available()) {
        log += "[kudroid_gpu] ABORT: JIT is Disabled — cannot execute ARM64 ELF\n";
    } else {
        kudroid::ElfLoader loader(path);
        if (!loader.parse()) {
            log += "[kudroid_gpu] PARSE FAILED: " + std::string(loader.lastError()) + "\n";
        } else if (!loader.map()) {
            log += "[kudroid_gpu] MAP FAILED: " + std::string(loader.lastError()) + "\n";
        } else if (!loader.relocate()) {
            log += "[kudroid_gpu] RELOCATE FAILED: " + std::string(loader.lastError()) + "\n";
        } else {
            log += "[kudroid_gpu] ELF mapped and Bionic imports bound.\n";
            void* address = loader.getSymbolAddress("kudroid_gpu_vulkan_test");
            if (!address) {
                log += "[kudroid_gpu] SYMBOL FAILED: kudroid_gpu_vulkan_test not found\n";
            } else {
                log += "[kudroid_gpu] Running kudroid_gpu_vulkan_test()...\n";
                mirrorCrash(log);
                using VkTestFn = int (*)(uint32_t*);
                uint32_t ext_count = 0;
                const int result = reinterpret_cast<VkTestFn>(address)(&ext_count);
                log += "[kudroid_gpu] VULKAN TEST RESULT: " +
                       std::to_string(result) + (result == 0 ? " (SUCCESS)" : " (FAILED)") + "\n";
                log += "[kudroid_gpu] Vulkan extensions found: " + std::to_string(ext_count) + "\n";
            }
        }
    }

    const char* shimTrace = kudroid::bionic_shim_trace();
    if (shimTrace && *shimTrace) {
        log += "[kudroid_gpu] Bionic shim trace:\n";
        log += shimTrace;
    }

    writeLogFile("kudroid_gpu_vulkan_test.txt", log);
    return strdup(log.c_str());
}

extern "C" const char* kudroid_gpu_opengl_so_test(const char* path) {
    std::string log;
    appendTestHeader(log, "GPU OpenGL+EGL .so Intercept Test", path);
    kudroid::bionic_shim_reset_trace();
    if (!path) {
        log += "[kudroid_gpu] ERROR: null path\n";
    } else if (!kudroid_jit_available()) {
        log += "[kudroid_gpu] ABORT: JIT is Disabled — cannot execute ARM64 ELF\n";
    } else {
        kudroid::ElfLoader loader(path);
        if (!loader.parse()) {
            log += "[kudroid_gpu] PARSE FAILED: " + std::string(loader.lastError()) + "\n";
        } else if (!loader.map()) {
            log += "[kudroid_gpu] MAP FAILED: " + std::string(loader.lastError()) + "\n";
        } else if (!loader.relocate()) {
            log += "[kudroid_gpu] RELOCATE FAILED: " + std::string(loader.lastError()) + "\n";
        } else {
            log += "[kudroid_gpu] ELF mapped and Bionic imports bound.\n";
            void* address = loader.getSymbolAddress("kudroid_gpu_opengl_test");
            if (!address) {
                log += "[kudroid_gpu] SYMBOL FAILED: kudroid_gpu_opengl_test not found\n";
            } else {
                log += "[kudroid_gpu] Running kudroid_gpu_opengl_test()...\n";
                mirrorCrash(log);
                using GlTestFn = int (*)();
                const int result = reinterpret_cast<GlTestFn>(address)();
                log += "[kudroid_gpu] OPENGL+EGL TEST RESULT: " +
                       std::to_string(result) + (result == 0 ? " (SUCCESS)" : " (FAILED)") + "\n";
            }
        }
    }

    const char* shimTrace = kudroid::bionic_shim_trace();
    if (shimTrace && *shimTrace) {
        log += "[kudroid_gpu] Bionic shim trace:\n";
        log += shimTrace;
    }

    writeLogFile("kudroid_gpu_opengl_test.txt", log);
    return strdup(log.c_str());
}

#include "kudroid/APKExtractor.h"
#include "kudroid/DexManager.h"

extern "C" const char* kudroid_load_apk(const char* apkPath) {
    static std::string log;
    log.clear();
    appendTestHeader(log, "APK Loader execution", apkPath);
    
    if (!apkPath) {
        log += "[kudroid_apk] ERROR: APK path is null\n";
        return log.c_str();
    }
    
    std::string apkStr(apkPath);
    std::string targetDir = std::string(g_logDir) + "/extracted_apk";
    
    log += "[kudroid_apk] Initializing VFS...\n";
    kudroid::VFSPathRemapper::getInstance().setDocumentsDirectory(g_logDir);
    
    log += "[kudroid_apk] Extracting APK to: " + targetDir + "\n";
    if (!kudroid::APKExtractor::extract_apk(apkStr, targetDir)) {
        log += "[kudroid_apk] ERROR: Extraction failed - " + kudroid::APKExtractor::lastError() + "\n";
        return log.c_str();
    }
    log += "[kudroid_apk] Extracted successfully.\n";
    
    log += "[kudroid_apk] Loading DEX files...\n";
    kudroid::DexManager::getInstance().loadDirectory(targetDir);
    
    log += "[kudroid_apk] Scanning for native libraries (.so)...\n";
    kudroid::LibraryManager libManager;
    std::string libDir = targetDir + "/lib/arm64-v8a";
    
    if (std::filesystem::exists(libDir)) {
        for (const auto& entry : std::filesystem::directory_iterator(libDir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".so") {
                log += "[kudroid_apk] Loading library: " + entry.path().filename().string() + "\n";
                if (!libManager.loadRecursive(entry.path().string())) {
                    log += "[kudroid_apk] WARNING: Failed to load " + entry.path().filename().string() + "\n";
                } else {
                    log += "[kudroid_apk] Loaded successfully.\n";
                    // cố gắng gọi jni_onload
                    void* jniOnLoadAddr = libManager.resolveGlobalSymbol("JNI_OnLoad");
                    if (jniOnLoadAddr) {
                        log += "[kudroid_apk] Found JNI_OnLoad in library, executing...\n";
                        using JNI_OnLoad_t = jint (*)(JavaVM*, void*);
                        JNI_OnLoad_t jniOnLoad = reinterpret_cast<JNI_OnLoad_t>(jniOnLoadAddr);
                        jint version = jniOnLoad(kudroid_jni_get_javavm(), nullptr);
                        log += "[kudroid_apk] JNI_OnLoad returned version: 0x" + std::to_string(version) + "\n";
                    }
                }
            }
        }
    } else {
        log += "[kudroid_apk] No native libraries found in " + libDir + "\n";
    }
    
    log += "[kudroid_apk] APK Load Complete.\n";
    return log.c_str();
}

// ─────────────────────────────────────────────────────────────────────────────
// dịch dex → jar (có bộ đệm)
//
// dịch một tệp dex thành một tệp jar của các lớp giả (thông qua dextojar), sử dụng
// dexcachemanager để lưu trữ kết quả được khóa bằng mã băm dex + phiên bản công cụ.
// trả về một chuỗi nhật ký được malloc; người gọi phải giải phóng nó bằng free().
// ─────────────────────────────────────────────────────────────────────────────
extern "C" const char* kudroid_translate_dex(const char* dexPath) {
    if (!dexPath || !*dexPath) {
        return strdup("[kudroid_dex] ERROR: null DEX path\n");
    }

    // phiên bản công cụ — tăng số này khi logic trình biên dịch thay đổi.
    const int kDexToolVersion = 1;

    std::string log;
    log += "[kudroid_dex] Translating DEX: " + std::string(dexPath) + "\n";

    auto& cache = kudroid::DexCacheManager::getInstance();
    if (cache.cacheDirectory().empty()) {
        // thư mục bộ đệm mặc định: documents/android_cache (được thiết lập thông qua kudroid_set_log_dir).
        if (g_logDir[0]) {
            cache.setCacheDirectory(std::string(g_logDir) + "/android_cache");
        }
    }

    std::vector<uint8_t> jar;
    std::string error;
    if (!cache.translateAndCache(dexPath, kDexToolVersion, jar, &error)) {
        log += "[kudroid_dex] TRANSLATE FAILED: " + error + "\n";
        writeLogFile("kudroid_dex_translate.txt", log);
        return strdup(log.c_str());
    }

    log += "[kudroid_dex] Translation OK: " + std::to_string(jar.size()) + " bytes JAR\n";
    log += "[kudroid_dex] Cache dir: " + cache.cacheDirectory() + "\n";

    // ghi tệp jar vào đĩa để avian có thể tải nó dưới dạng đường dẫn lớp.
    std::string jarPath = std::string(g_logDir) + "/translated_classes.jar";
    FILE* f = std::fopen(jarPath.c_str(), "wb");
    if (f) {
        std::fwrite(jar.data(), 1, jar.size(), f);
        std::fclose(f);
        log += "[kudroid_dex] Wrote JAR: " + jarPath + "\n";
    } else {
        log += "[kudroid_dex] WARNING: cannot write JAR to " + jarPath + "\n";
    }

    writeLogFile("kudroid_dex_translate.txt", log);
    return strdup(log.c_str());
}