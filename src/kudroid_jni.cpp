#include "kudroid/kudroid_jni.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <mutex>
#include <string>
#include <vector>

// Pre-include mimalloc to prevent miniJVM's d_type.h from wrapping it in extern "C", 
// which breaks C++ template definitions inside mimalloc.h
#include "../utils/mimalloc/include/mimalloc.h"

// miniJVM includes
#include "jvm.h"
#include "jvm_util.h"

// Global state
static MiniJVM* g_jvm = nullptr;
static JNINativeInterface_ g_jni_interface;
static JNIInvokeInterface_ g_invoke_interface;
static JNIEnv_ g_jni_env = { &g_jni_interface };
static JavaVM_ g_java_vm = { &g_invoke_interface };
static std::mutex g_jvm_mutex;
#include <functional>
#include <vector>

static std::function<void(const char*)> g_jni_log_callback;

extern "C" void kudroid_jni_set_log_callback(void (*cb)(const char*)) {
    if (cb) {
        g_jni_log_callback = [cb](const char* msg) { cb(msg); };
    } else {
        g_jni_log_callback = nullptr;
    }
}

// Helper for detailed logging
static void log_jni(const char* fmt, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    fprintf(stderr, "[kudroid_jni] %s\n", buffer);

    if (g_jni_log_callback) {
        g_jni_log_callback(buffer);
    }
}

// ----------------------------------------------------------------------------
// JVM Lifecycle Methods
// ----------------------------------------------------------------------------

void kudroid_jni_init_jvm(const char* bootclasspath, const char* classpath) {
    std::lock_guard<std::mutex> lock(g_jvm_mutex);
    if (!g_jvm) {
        log_jni("Initializing miniJVM memory allocator...");
        jvm_init_mem_alloc();
        
        log_jni("Creating miniJVM instance...");
        g_jvm = jvm_create();
        if (!g_jvm) {
            log_jni("ERROR: Failed to create miniJVM instance.");
            return;
        }
        
        log_jni("Initializing miniJVM with bootclasspath='%s', classpath='%s'", bootclasspath, classpath);
        int ret = jvm_init(g_jvm, (c8*)bootclasspath, (c8*)classpath);
        if (ret != 0) {
            log_jni("WARNING: jvm_init returned %d (expected if rt.jar is missing)", ret);
        } else {
            log_jni("JVM initialized successfully.");
        }
    }
}

void kudroid_jni_destroy_jvm(void) {
    std::lock_guard<std::mutex> lock(g_jvm_mutex);
    if (g_jvm) {
        log_jni("Destroying miniJVM instance...");
        jvm_destroy(g_jvm);
        jvm_destroy_mem_alloc();
        g_jvm = nullptr;
    }
}

JavaVM* kudroid_jni_get_javavm(void) {
    return &g_java_vm;
}

jint kudroid_jni_get_env(JavaVM* vm, void** env, jint version) {
    (void)vm;
    (void)version;
    kudroid_jni_init_jvm("", ""); // Init with empty paths for now if not already init
    *env = &g_jni_env;
    return JNI_OK;
}

// ----------------------------------------------------------------------------
// Generic Dummy / Stub handler
// ----------------------------------------------------------------------------

static void* kudroid_jni_dummy(JNIEnv* env, ...) {
    // In AAPCS64 (ARM64), reading arguments from varargs without knowing types is tricky.
    // We just log that an unimplemented function was called.
    log_jni("WARNING: An unimplemented JNI function was called! Returning NULL/0.");
    return nullptr;
}

// ----------------------------------------------------------------------------
// JNI API Implementations
// ----------------------------------------------------------------------------

static jint JNICALL jni_GetVersion(JNIEnv* env) {
    log_jni("GetVersion() called");
    return JNI_VERSION_1_6;
}

static jclass JNICALL jni_FindClass(JNIEnv* env, const char* name) {
    log_jni("FindClass(name='%s')", name);
    if (!g_jvm) return nullptr;
    
    // Create a temporary runtime for the lookup
    Runtime* runtime = runtime_create_inl(nullptr);
    runtime->jvm = g_jvm;
    
    // miniJVM uses "java/lang/String", same as JNI
    JClass* clazz = classes_load_get_with_clinit_c(nullptr, name, runtime);
    
    runtime_destroy_inl(runtime);
    
    if (!clazz) {
        log_jni("FindClass: Class '%s' not found!", name);
    } else {
        log_jni("FindClass: Successfully found '%s' at %p", name, clazz);
    }
    
    return reinterpret_cast<jclass>(clazz);
}

static jmethodID JNICALL jni_GetMethodID(JNIEnv* env, jclass clazz, const char* name, const char* sig) {
    log_jni("GetMethodID(clazz=%p, name='%s', sig='%s')", clazz, name, sig);
    if (!clazz) return nullptr;
    
    JClass* jc = reinterpret_cast<JClass*>(clazz);
    Runtime* runtime = runtime_create_inl(nullptr);
    runtime->jvm = g_jvm;
    
    MethodInfo* method = find_methodInfo_by_name_c(utf8_cstr(jc->name), name, sig, jc->jloader, runtime);
    
    runtime_destroy_inl(runtime);
    
    if (!method) {
        log_jni("GetMethodID: Method %s%s not found in class %s", name, sig, utf8_cstr(jc->name));
    } else {
        log_jni("GetMethodID: Found method at %p", method);
    }
    
    return reinterpret_cast<jmethodID>(method);
}

static jmethodID JNICALL jni_GetStaticMethodID(JNIEnv* env, jclass clazz, const char* name, const char* sig) {
    log_jni("GetStaticMethodID(clazz=%p, name='%s', sig='%s')", clazz, name, sig);
    return jni_GetMethodID(env, clazz, name, sig);
}

// String creation
static jstring JNICALL jni_NewStringUTF(JNIEnv* env, const char* bytes) {
    log_jni("NewStringUTF(bytes='%s')", bytes ? bytes : "(null)");
    if (!bytes || !g_jvm) return nullptr;
    
    Runtime* runtime = runtime_create_inl(nullptr);
    runtime->jvm = g_jvm;
    
    JClass* str_class = classes_load_get_with_clinit_c(nullptr, "java/lang/String", runtime);
    if (!str_class) {
        log_jni("ERROR: java/lang/String not found (missing rt.jar?). Cannot create jstring.");
        runtime_destroy_inl(runtime);
        return nullptr;
    }
    
    Instance* jstr = jstring_create_cstr(bytes, runtime);
    
    runtime_destroy_inl(runtime);
    return reinterpret_cast<jstring>(jstr);
}

static const char* JNICALL jni_GetStringUTFChars(JNIEnv* env, jstring string, jboolean* isCopy) {
    log_jni("GetStringUTFChars(string=%p)", string);
    if (!string || !g_jvm) return nullptr;
    
    Instance* jstr = reinterpret_cast<Instance*>(string);
    Runtime* runtime = runtime_create_inl(nullptr);
    runtime->jvm = g_jvm;
    
    Utf8String* ustr = utf8_create();
    jstring_2_utf8(jstr, ustr, runtime);
    
    // Convert to C string
    char* cstr = strdup(utf8_cstr(ustr));
    utf8_destroy(ustr);
    
    runtime_destroy_inl(runtime);
    
    if (isCopy) *isCopy = JNI_TRUE;
    return cstr;
}

static void JNICALL jni_ReleaseStringUTFChars(JNIEnv* env, jstring string, const char* utf) {
    log_jni("ReleaseStringUTFChars(utf='%s')", utf ? utf : "(null)");
    if (utf) {
        free(const_cast<char*>(utf));
    }
}

static jsize JNICALL jni_GetStringUTFLength(JNIEnv* env, jstring string) {
    log_jni("GetStringUTFLength(string=%p)", string);
    if (!string) return 0;
    
    // We can just get the chars and measure, or check the backing char array.
    // For simplicity right now, convert and measure.
    const char* cstr = jni_GetStringUTFChars(env, string, nullptr);
    jsize len = cstr ? strlen(cstr) : 0;
    jni_ReleaseStringUTFChars(env, string, cstr);
    return len;
}

static jint JNICALL jni_RegisterNatives(JNIEnv* env, jclass clazz, const JNINativeMethod* methods, jint nMethods) {
    log_jni("RegisterNatives(clazz=%p, nMethods=%d)", clazz, nMethods);
    if (!clazz || !methods) return JNI_ERR;
    
    JClass* jc = reinterpret_cast<JClass*>(clazz);
    Runtime* runtime = runtime_create_inl(nullptr);
    runtime->jvm = g_jvm;
    
    for (jint i = 0; i < nMethods; ++i) {
        log_jni("  Registering native method: %s%s -> %p", methods[i].name, methods[i].signature, methods[i].fnPtr);
        MethodInfo* method = find_methodInfo_by_name_c(utf8_cstr(jc->name), methods[i].name, methods[i].signature, jc->jloader, runtime);
        
        if (method) {
            // In miniJVM, native_func is usually checked. We can override the C function pointer.
            // Note: miniJVM expects native methods to have a specific signature:
            // s32 func(Runtime *runtime, JClass *clazz)
            // But JNI expects (JNIEnv*, jclass/jobject, ...).
            // This requires a thunk/trampoline if we want to call JNI functions from miniJVM correctly.
            // For now, just logging it. Real trampoline logic will be needed if miniJVM calls out to JNI.
            log_jni("  [TODO] Trampoline generation needed to bridge miniJVM -> JNI calling convention.");
        } else {
            log_jni("  WARNING: Method %s%s not found in class %s", methods[i].name, methods[i].signature, utf8_cstr(jc->name));
        }
    }
    
    runtime_destroy_inl(runtime);
    return JNI_OK;
}

// ----------------------------------------------------------------------------
// Initialization
// ----------------------------------------------------------------------------

#include "kudroid_jni_impl.inc"

__attribute__((constructor))
static void kudroid_jni_init_tables() {
    // 1. Initialize all standard 230+ JNI functions from auto-generated bridge
    init_generated_jni_interface(&g_jni_interface);
    
    // 2. Map implemented functions
    g_jni_interface.GetVersion = jni_GetVersion;
    g_jni_interface.FindClass = jni_FindClass;
    
    g_jni_interface.GetMethodID = jni_GetMethodID;
    g_jni_interface.GetStaticMethodID = jni_GetStaticMethodID;
    
    g_jni_interface.NewStringUTF = jni_NewStringUTF;
    g_jni_interface.GetStringUTFChars = jni_GetStringUTFChars;
    g_jni_interface.ReleaseStringUTFChars = jni_ReleaseStringUTFChars;
    g_jni_interface.GetStringUTFLength = jni_GetStringUTFLength;
    
    g_jni_interface.RegisterNatives = jni_RegisterNatives;
    
    // 3. Fill JNIInvokeInterface
    g_invoke_interface.DestroyJavaVM = [](JavaVM* vm) -> jint {
        log_jni("DestroyJavaVM called");
        kudroid_jni_destroy_jvm();
        return JNI_OK;
    };
    g_invoke_interface.AttachCurrentThread = [](JavaVM* vm, void** env, void* args) -> jint {
        log_jni("AttachCurrentThread called");
        return kudroid_jni_get_env(vm, env, 0);
    };
    g_invoke_interface.DetachCurrentThread = [](JavaVM* vm) -> jint {
        log_jni("DetachCurrentThread called");
        return JNI_OK;
    };
    g_invoke_interface.GetEnv = kudroid_jni_get_env;
    g_invoke_interface.AttachCurrentThreadAsDaemon = g_invoke_interface.AttachCurrentThread;
}
