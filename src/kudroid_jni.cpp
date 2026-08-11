#include "kudroid/kudroid_jni.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <mutex>
#include <string>
#include <functional>

// ─────────────────────────────────────────────────────────────────────────────
// KuDroid JNI bridge — now backed by the Avian JVM.
//
// Avian provides a real, complete JNI implementation. We create the VM via the
// standard JNI_CreateJavaVM entry point (exported by the Avian static library)
// and hand the resulting JavaVM*/JNIEnv* straight to the Android .so libraries.
//
// The boot classpath is provided by an embedded classpath jar (see the
// "Embedded boot classpath" section below), which is compiled into the Avian
// static library by the build system. The framework classes (android.*) are
// merged into that jar.
// ─────────────────────────────────────────────────────────────────────────────

// JNI_CreateJavaVM is exported by the Avian static library. Declare it here so
// we don't need to include Avian's internal headers (which pull in a lot).
extern "C" jint JNI_CreateJavaVM(JavaVM** p_vm, void** p_env, void* vm_args);

// Global state
static JavaVM* g_vm = nullptr;
static JNIEnv* g_env = nullptr;
static std::mutex g_jvm_mutex;
static std::mutex g_log_mutex;

static std::function<void(const char*)> g_jni_log_callback;

extern "C" void kudroid_jni_set_log_callback(void (*cb)(const char*)) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    if (cb) {
        g_jni_log_callback = [cb](const char* msg) { cb(msg); };
    } else {
        g_jni_log_callback = nullptr;
    }
}

// Helper for detailed logging (thread-safe).
static void log_jni(const char* fmt, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    fprintf(stderr, "[kudroid_jni] %s\n", buffer);

    std::lock_guard<std::mutex> lock(g_log_mutex);
    if (g_jni_log_callback) {
        g_jni_log_callback(buffer);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Embedded boot classpath
//
// The Avian boot classpath is provided as an embedded jar. Avian's default
// build (no bootimage) embeds the classpath jar via the `classpath-jar.o`
// object (built from an .incbin assembly stub by scripts/patch-avian-ios.py)
// and exposes it through the `classpathJar()` symbol in boot.cpp. We select it
// with the "-Xbootclasspath:[classpathJar]" option.
//
// The framework classes (android.*) are merged into that same classpath jar by
// the patched Avian makefile, so they are available on the boot classpath.
// ─────────────────────────────────────────────────────────────────────────────

#if (defined __MINGW32__) || (defined _MSC_VER)
#  define KUDROID_EXPORT __declspec(dllexport)
#else
#  define KUDROID_EXPORT __attribute__ ((visibility("default"))) __attribute__ ((used))
#endif

extern "C" void __cxa_pure_virtual(void) { abort(); }

// ─────────────────────────────────────────────────────────────────────────────
// JVM Lifecycle Methods
// ─────────────────────────────────────────────────────────────────────────────

void kudroid_jni_init_jvm(const char* bootclasspath, const char* classpath) {
    (void)bootclasspath; // Avian always uses the embedded classpath jar
    std::lock_guard<std::mutex> lock(g_jvm_mutex);
    if (g_vm) {
        return; // Already initialized
    }

    log_jni("Initializing Avian JVM...");

    // Build the VM arguments. We always use the embedded classpath jar as the
    // boot classpath. The caller-provided classpath is appended as an
    // additional classpath entry (for app classes loaded at runtime).
    std::string bootOption = "-Xbootclasspath:[classpathJar]";
    std::string classpathOption;
    if (classpath && classpath[0] != '\0') {
        classpathOption = std::string("-Xbootclasspath/a:") + classpath;
    }

    // Limit the heap to a reasonable size for iOS (avoids memory pressure).
    // Avian accepts -Xmx<N>m.
    std::string heapOption = "-Xmx256m";

    // Count options: classpath jar + heap are always present; classpath is optional.
    int nOptions = 2;
    if (!classpathOption.empty()) nOptions++;

    JavaVMOption options[3];
    options[0].optionString = const_cast<char*>(bootOption.c_str());
    options[1].optionString = const_cast<char*>(heapOption.c_str());
    if (!classpathOption.empty()) {
        options[2].optionString = const_cast<char*>(classpathOption.c_str());
    }

    JavaVMInitArgs vmArgs;
    vmArgs.version = JNI_VERSION_1_6;
    vmArgs.nOptions = nOptions;
    vmArgs.options = options;
    vmArgs.ignoreUnrecognized = JNI_TRUE;

    void* env = nullptr;
    jint result = JNI_CreateJavaVM(&g_vm, &env, &vmArgs);
    if (result != JNI_OK || !g_vm || !env) {
        log_jni("ERROR: JNI_CreateJavaVM failed with code %d", result);
        g_vm = nullptr;
        g_env = nullptr;
        return;
    }

    g_env = static_cast<JNIEnv*>(env);
    log_jni("Avian JVM initialized successfully (JavaVM=%p, JNIEnv=%p)",
            (void*)g_vm, (void*)g_env);
}

void kudroid_jni_destroy_jvm(void) {
    std::lock_guard<std::mutex> lock(g_jvm_mutex);
    if (g_vm) {
        log_jni("Destroying Avian JVM...");
        g_vm->DestroyJavaVM();
        g_vm = nullptr;
        g_env = nullptr;
    }
}

extern "C" JavaVM* kudroid_jni_get_javavm(void) {
    std::lock_guard<std::mutex> lock(g_jvm_mutex);
    return g_vm;
}

jint kudroid_jni_get_env(JavaVM* vm, void** env, jint version) {
    (void)vm;
    (void)version;
    kudroid_jni_init_jvm("", ""); // Init if not already

    std::lock_guard<std::mutex> lock(g_jvm_mutex);
    if (!g_vm || !env) {
        return JNI_ERR;
    }

    // JNIEnv is per-thread in a real JVM. If the calling thread is not the
    // thread that created the VM, attach it to get its own JNIEnv.
    JNIEnv* threadEnv = nullptr;
    jint status = g_vm->GetEnv(reinterpret_cast<void**>(&threadEnv), JNI_VERSION_1_6);
    if (status == JNI_OK && threadEnv) {
        *env = threadEnv;
        return JNI_OK;
    }
    if (status == JNI_EDETACHED) {
        // Attach this thread to the VM.
        status = g_vm->AttachCurrentThread(reinterpret_cast<void**>(&threadEnv), nullptr);
        if (status == JNI_OK && threadEnv) {
            *env = threadEnv;
            return JNI_OK;
        }
    }
    // Fall back to the main env (best effort).
    if (g_env) {
        *env = g_env;
        return JNI_OK;
    }
    return JNI_ERR;
}
