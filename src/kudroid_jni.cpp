#include "kudroid/kudroid_jni.h"
#include "kudroid/VFSPathRemapper.h"
#include "kudroid/AutoStub.h"
#include "kudroid/framework_jar_bytes.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <mutex>
#include <sys/stat.h>
#include <string>
#include <functional>
#include <pthread.h>

// Build stamp (version + commit hash) — định nghĩa trong kudroid_bridge.cpp.
extern "C" const char* kudroid_build_stamp(void);

// ─────────────────────────────────────────────────────────────────────────────
// bộ nối jni kudroid — hiện được hỗ trợ bởi jvm avian.
//
// avian cung cấp một triển khai jni thực sự, hoàn chỉnh. chúng tôi tạo máy ảo thông qua
// điểm vào jni_createjavavm tiêu chuẩn (được xuất bởi thư viện tĩnh avian)
// và trao javavm*/jnienv* kết quả trực tiếp cho các thư viện .so của android.
//
// đường dẫn lớp khởi động được cung cấp bởi một tệp jar đường dẫn lớp được nhúng (xem
// phần "đường dẫn lớp khởi động được nhúng" bên dưới), được biên dịch thành thư viện
// tĩnh avian bởi hệ thống xây dựng. các lớp khung (android.*) được
// hợp nhất vào tệp jar đó.
// ─────────────────────────────────────────────────────────────────────────────

// jni_createjavavm được xuất bởi thư viện tĩnh avian. khai báo nó ở đây để
// chúng ta không cần bao gồm các tiêu đề nội bộ của avian (kéo theo rất nhiều thứ).
extern "C" jint JNI_CreateJavaVM(JavaVM** p_vm, void** p_env, void* vm_args);

// classpathJar() (định nghĩa trong boot.cpp của avian) trả về boot classpath jar
// được nhúng trong libavian.a qua classpath-jar.o. Gọi nó một lần khi khởi tạo JVM
// vừa để log kích thước jar (debug), vừa TẠO LINK-TIME REFERENCE ép linker kéo
// boot.o + classpath-jar.o vào app — nếu không, 2 object này nằm trong archive
// nhưng không được kéo (classpathJar chỉ được gọi qua dlsym runtime) → boot
// classpath rỗng → FindClass('java/lang/String') trả NULL → GetSuperclass(NULL) crash.
extern "C" const uint8_t* classpathJar(size_t* size);

// Forward về pipeline log chuẩn của kudroid (định nghĩa trong SyscallShim.cpp).
extern "C" int kudroid_android_log_message(int priority, const char* tag, const char* message);

// Kích thước màn hình thật do Swift bắn qua kudroid_set_metal_layer
// (định nghĩa trong kudroid_bridge.cpp).
extern "C" int g_metalLayerWidth;
extern "C" int g_metalLayerHeight;
extern "C" float g_metalLayerDensity;

// Đẩy số liệu màn hình vào DisplayMetrics (định nghĩa cuối file này).
extern "C" void kudroid_jni_update_display_metrics(void);

// log_jni được định nghĩa phía dưới trong file này.
static void log_jni(const char* fmt, ...);

// trạng thái toàn cục
static JavaVM* g_vm = nullptr;
static JNIEnv* g_env = nullptr;
static std::mutex g_jvm_mutex;
static std::mutex g_log_mutex;

// pthread_key để DetachCurrentThread khi thread (do host tạo) kết thúc.
static pthread_key_t g_jni_attach_key;
static pthread_once_t g_jni_attach_once = PTHREAD_ONCE_INIT;

static void jni_detach_destructor(void* /*marker*/) {
    // Chạy khi thread kết thúc; g_vm có thể đã bị hủy (nullptr) -> bỏ qua.
    JavaVM* vm = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_jvm_mutex);
        vm = g_vm;
    }
    if (vm) {
        vm->DetachCurrentThread();
    }
}

static void init_jni_attach_key() {
    ::pthread_key_create(&g_jni_attach_key, jni_detach_destructor);
}

// ─────────────────────────────────────────────────────────────────────────────
// android.util.Log.println_native
//
// Framework khai báo native method này nhưng trước đây KHÔNG có registration nào
// (không có symbol mangled Java_/Avian_, không RegisterNatives) → UnsatisfiedLinkError
// ở lần gọi Log.* đầu tiên từ Java (Toast, glue Unity/Godot/SDL). Forward sang
// kudroid_android_log_message để log rơi vào đúng pipeline (stdout + file + crash buffer).
// ─────────────────────────────────────────────────────────────────────────────
static jint java_android_util_Log_println_native(JNIEnv* env, jclass /*clazz*/,
                                                 jint priority, jstring tag, jstring msg) {
    const char* tagC = tag ? env->GetStringUTFChars(tag, nullptr) : nullptr;
    const char* msgC = msg ? env->GetStringUTFChars(msg, nullptr) : nullptr;
    const int result = kudroid_android_log_message(
        static_cast<int>(priority),
        tagC ? tagC : "Java",
        msgC ? msgC : "");
    if (tagC) env->ReleaseStringUTFChars(tag, tagC);
    if (msgC) env->ReleaseStringUTFChars(msg, msgC);
    return result;
}

// Đăng ký println_native ngay sau khi JVM được tạo — trước khi bất kỳ lớp Java nào
// (kể cả glue của game) gọi Log.*.
static void register_android_util_log_natives(JNIEnv* env) {
    jclass clazz = env->FindClass("android/util/Log");
    if (!clazz) {
        // ĐỪNG bỏ qua im lặng: nếu framework jar không có Log trong boot classpath,
        // Java gọi Log.* sẽ UnsatisfiedLinkError sập JVM. Báo lỗi rõ ràng.
        if (env->ExceptionCheck()) env->ExceptionClear();
        log_jni("ERROR: FindClass(android/util/Log) failed — framework classes missing "
                "from boot classpath (re-run framework/build.sh + avian make)");
        return;
    }
    // Avian's JNINativeMethod uses char* (not const char*); cast the literals.
    static const JNINativeMethod methods[] = {
        {const_cast<char*>("println_native"),
         const_cast<char*>("(ILjava/lang/String;Ljava/lang/String;)I"),
         reinterpret_cast<void*>(&java_android_util_Log_println_native)},
    };
    if (env->RegisterNatives(clazz, methods, 1) != JNI_OK) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        log_jni("ERROR: RegisterNatives(android/util/Log.println_native) failed — "
                "Java calls to Log.* will throw UnsatisfiedLinkError");
        return;
    }
    log_jni("Registered android/util/Log.println_native -> C++ log pipeline");
    env->DeleteLocalRef(clazz);
}

// ─────────────────────────────────────────────────────────────────────────────
// android.graphics.Canvas native methods -> JavaCanvasRenderer
// ─────────────────────────────────────────────────────────────────────────────
#include "kudroid/platform/JavaCanvasRenderer.h"

static void java_android_graphics_Canvas_native_drawColor(JNIEnv* /*env*/, jclass /*clazz*/, jint color) {
    kudroid::JavaCanvasRenderer::getInstance().drawColor(static_cast<uint32_t>(color));
}

static void java_android_graphics_Canvas_native_drawRect(JNIEnv* /*env*/, jclass /*clazz*/, jfloat left, jfloat top, jfloat right, jfloat bottom, jint color) {
    kudroid::JavaCanvasRenderer::getInstance().drawRect(left, top, right, bottom, static_cast<uint32_t>(color));
}

static void java_android_graphics_Canvas_native_drawText(JNIEnv* env, jclass /*clazz*/, jstring text, jfloat x, jfloat y, jint color, jfloat textSize) {
    if (!text || !env) return;
    const char* str = env->GetStringUTFChars(text, nullptr);
    if (str) {
        kudroid::JavaCanvasRenderer::getInstance().drawText(str, x, y, static_cast<uint32_t>(color), textSize);
        env->ReleaseStringUTFChars(text, str);
    }
}

static void java_android_graphics_Canvas_native_drawBitmap(JNIEnv* env, jclass /*clazz*/, jintArray pixels, jint width, jint height, jfloat x, jfloat y) {
    if (!pixels || !env || width <= 0 || height <= 0) return;
    jint* p = env->GetIntArrayElements(pixels, nullptr);
    if (p) {
        kudroid::JavaCanvasRenderer::getInstance().drawBitmap(reinterpret_cast<const uint32_t*>(p), width, height, x, y);
        env->ReleaseIntArrayElements(pixels, p, JNI_ABORT);
    }
}

static void java_android_graphics_Canvas_native_flush(JNIEnv* /*env*/, jclass /*clazz*/) {
    kudroid::JavaCanvasRenderer::getInstance().flush();
}

static void register_android_graphics_canvas_natives(JNIEnv* env) {
    jclass clazz = env->FindClass("android/graphics/Canvas");
    if (!clazz) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        return;
    }
    static const JNINativeMethod methods[] = {
        {const_cast<char*>("native_drawColor"), const_cast<char*>("(I)V"), reinterpret_cast<void*>(&java_android_graphics_Canvas_native_drawColor)},
        {const_cast<char*>("native_drawRect"), const_cast<char*>("(FFFFI)V"), reinterpret_cast<void*>(&java_android_graphics_Canvas_native_drawRect)},
        {const_cast<char*>("native_drawText"), const_cast<char*>("(Ljava/lang/String;FFIF)V"), reinterpret_cast<void*>(&java_android_graphics_Canvas_native_drawText)},
        {const_cast<char*>("native_drawBitmap"), const_cast<char*>("([IIIIFF)V"), reinterpret_cast<void*>(&java_android_graphics_Canvas_native_drawBitmap)},
        {const_cast<char*>("native_flush"), const_cast<char*>("()V"), reinterpret_cast<void*>(&java_android_graphics_Canvas_native_flush)},
    };
    if (env->RegisterNatives(clazz, methods, sizeof(methods) / sizeof(methods[0])) != JNI_OK) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        log_jni("ERROR: RegisterNatives(android/graphics/Canvas) failed");
        return;
    }
    log_jni("Registered android/graphics/Canvas -> C++ Metal Canvas pipeline");
    env->DeleteLocalRef(clazz);
}

// ─────────────────────────────────────────────────────────────────────────────
// java.lang.System.arraycopy JNI native implementation (pure JNI, 0% reflection)
// ─────────────────────────────────────────────────────────────────────────────
extern "C" JNIEXPORT void JNICALL
Java_java_lang_System_arraycopy(JNIEnv* env, jclass /*clazz*/,
                                jobject src, jint srcPos,
                                jobject dst, jint dstPos, jint length) {
    if (!src || !dst) {
        jclass npe = env->FindClass("java/lang/NullPointerException");
        if (npe) env->ThrowNew(npe, "src or dst is null");
        return;
    }
    if (srcPos < 0 || dstPos < 0 || length < 0) {
        jclass aioobe = env->FindClass("java/lang/IndexOutOfBoundsException");
        if (aioobe) env->ThrowNew(aioobe, "negative index or length");
        return;
    }
    if (length == 0) return;

    jsize srcLen = env->GetArrayLength(static_cast<jarray>(src));
    jsize dstLen = env->GetArrayLength(static_cast<jarray>(dst));

    if (srcPos + length > srcLen || dstPos + length > dstLen) {
        jclass aioobe = env->FindClass("java/lang/IndexOutOfBoundsException");
        if (aioobe) env->ThrowNew(aioobe, "array index out of bounds");
        return;
    }

    jclass byteArrCls = env->FindClass("[B");
    if (byteArrCls && env->IsInstanceOf(src, byteArrCls)) {
        jbyte* buf = static_cast<jbyte*>(std::malloc(length * sizeof(jbyte)));
        if (buf) {
            env->GetByteArrayRegion(static_cast<jbyteArray>(src), srcPos, length, buf);
            env->SetByteArrayRegion(static_cast<jbyteArray>(dst), dstPos, length, buf);
            std::free(buf);
        }
        env->DeleteLocalRef(byteArrCls);
        return;
    }
    if (byteArrCls) env->DeleteLocalRef(byteArrCls);

    jclass charArrCls = env->FindClass("[C");
    if (charArrCls && env->IsInstanceOf(src, charArrCls)) {
        jchar* buf = static_cast<jchar*>(std::malloc(length * sizeof(jchar)));
        if (buf) {
            env->GetCharArrayRegion(static_cast<jcharArray>(src), srcPos, length, buf);
            env->SetCharArrayRegion(static_cast<jcharArray>(dst), dstPos, length, buf);
            std::free(buf);
        }
        env->DeleteLocalRef(charArrCls);
        return;
    }
    if (charArrCls) env->DeleteLocalRef(charArrCls);

    jclass intArrCls = env->FindClass("[I");
    if (intArrCls && env->IsInstanceOf(src, intArrCls)) {
        jint* buf = static_cast<jint*>(std::malloc(length * sizeof(jint)));
        if (buf) {
            env->GetIntArrayRegion(static_cast<jintArray>(src), srcPos, length, buf);
            env->SetIntArrayRegion(static_cast<jintArray>(dst), dstPos, length, buf);
            std::free(buf);
        }
        env->DeleteLocalRef(intArrCls);
        return;
    }
    if (intArrCls) env->DeleteLocalRef(intArrCls);

    jclass shortArrCls = env->FindClass("[S");
    if (shortArrCls && env->IsInstanceOf(src, shortArrCls)) {
        jshort* buf = static_cast<jshort*>(std::malloc(length * sizeof(jshort)));
        if (buf) {
            env->GetShortArrayRegion(static_cast<jshortArray>(src), srcPos, length, buf);
            env->SetShortArrayRegion(static_cast<jshortArray>(dst), dstPos, length, buf);
            std::free(buf);
        }
        env->DeleteLocalRef(shortArrCls);
        return;
    }
    if (shortArrCls) env->DeleteLocalRef(shortArrCls);

    jclass boolArrCls = env->FindClass("[Z");
    if (boolArrCls && env->IsInstanceOf(src, boolArrCls)) {
        jboolean* buf = static_cast<jboolean*>(std::malloc(length * sizeof(jboolean)));
        if (buf) {
            env->GetBooleanArrayRegion(static_cast<jbooleanArray>(src), srcPos, length, buf);
            env->SetBooleanArrayRegion(static_cast<jbooleanArray>(dst), dstPos, length, buf);
            std::free(buf);
        }
        env->DeleteLocalRef(boolArrCls);
        return;
    }
    if (boolArrCls) env->DeleteLocalRef(boolArrCls);

    jclass longArrCls = env->FindClass("[J");
    if (longArrCls && env->IsInstanceOf(src, longArrCls)) {
        jlong* buf = static_cast<jlong*>(std::malloc(length * sizeof(jlong)));
        if (buf) {
            env->GetLongArrayRegion(static_cast<jlongArray>(src), srcPos, length, buf);
            env->SetLongArrayRegion(static_cast<jlongArray>(dst), dstPos, length, buf);
            std::free(buf);
        }
        env->DeleteLocalRef(longArrCls);
        return;
    }
    if (longArrCls) env->DeleteLocalRef(longArrCls);

    jclass floatArrCls = env->FindClass("[F");
    if (floatArrCls && env->IsInstanceOf(src, floatArrCls)) {
        jfloat* buf = static_cast<jfloat*>(std::malloc(length * sizeof(jfloat)));
        if (buf) {
            env->GetFloatArrayRegion(static_cast<jfloatArray>(src), srcPos, length, buf);
            env->SetFloatArrayRegion(static_cast<jfloatArray>(dst), dstPos, length, buf);
            std::free(buf);
        }
        env->DeleteLocalRef(floatArrCls);
        return;
    }
    if (floatArrCls) env->DeleteLocalRef(floatArrCls);

    jclass doubleArrCls = env->FindClass("[D");
    if (doubleArrCls && env->IsInstanceOf(src, doubleArrCls)) {
        jdouble* buf = static_cast<jdouble*>(std::malloc(length * sizeof(jdouble)));
        if (buf) {
            env->GetDoubleArrayRegion(static_cast<jdoubleArray>(src), srcPos, length, buf);
            env->SetDoubleArrayRegion(static_cast<jdoubleArray>(dst), dstPos, length, buf);
            std::free(buf);
        }
        env->DeleteLocalRef(doubleArrCls);
        return;
    }
    if (doubleArrCls) env->DeleteLocalRef(doubleArrCls);

    // Mảng Object[]
    if (src == dst && srcPos < dstPos) {
        for (jint i = length - 1; i >= 0; --i) {
            jobject elem = env->GetObjectArrayElement(static_cast<jobjectArray>(src), srcPos + i);
            env->SetObjectArrayElement(static_cast<jobjectArray>(dst), dstPos + i, elem);
            if (elem) env->DeleteLocalRef(elem);
        }
    } else {
        for (jint i = 0; i < length; ++i) {
            jobject elem = env->GetObjectArrayElement(static_cast<jobjectArray>(src), srcPos + i);
            env->SetObjectArrayElement(static_cast<jobjectArray>(dst), dstPos + i, elem);
            if (elem) env->DeleteLocalRef(elem);
        }
    }
}

static void register_java_lang_system_natives(JNIEnv* env) {
    jclass clazz = env->FindClass("java/lang/System");
    if (!clazz) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        log_jni("ERROR: FindClass(java/lang/System) failed");
        return;
    }
    static const JNINativeMethod methods[] = {
        {const_cast<char*>("arraycopy"),
         const_cast<char*>("(Ljava/lang/Object;ILjava/lang/Object;II)V"),
         reinterpret_cast<void*>(&Java_java_lang_System_arraycopy)},
    };
    if (env->RegisterNatives(clazz, methods, 1) != JNI_OK) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        log_jni("ERROR: RegisterNatives(java/lang/System.arraycopy) failed");
        return;
    }
    log_jni("Registered java/lang/System.arraycopy -> JNI native bridge");
    env->DeleteLocalRef(clazz);
}

static std::function<void(const char*)> g_jni_log_callback;

extern "C" void kudroid_jni_set_log_callback(void (*cb)(const char*)) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    if (cb) {
        g_jni_log_callback = [cb](const char* msg) { cb(msg); };
    } else {
        g_jni_log_callback = nullptr;
    }
}

// hàm hỗ trợ để ghi nhật ký chi tiết (an toàn luồng).
#include <unordered_map>

static struct JNINativeInterface_ g_interposedJniFunctions;
static bool g_interposedFunctionsInitialized = false;

struct DirectBufferEntry {
    void* address;
    jlong capacity;
};
static std::unordered_map<void*, DirectBufferEntry> g_directBuffers;
static std::mutex g_directBufferMutex;

static jobject JNICALL kudroid_shim_NewDirectByteBuffer(JNIEnv* env, void* address, jlong capacity) {
    if (!env) return nullptr;
    jbyteArray arr = env->NewByteArray((jsize)(capacity > 0 ? 1 : 0));
    if (!arr) return nullptr;
    jobject gref = env->NewGlobalRef(arr);
    env->DeleteLocalRef(arr);

    std::lock_guard<std::mutex> lock(g_directBufferMutex);
    g_directBuffers[gref] = { address, capacity };
    return gref;
}

static void* JNICALL kudroid_shim_GetDirectBufferAddress(JNIEnv* env, jobject buf) {
    (void)env;
    if (!buf) return nullptr;
    std::lock_guard<std::mutex> lock(g_directBufferMutex);
    auto it = g_directBuffers.find(buf);
    if (it != g_directBuffers.end()) {
        return it->second.address;
    }
    return nullptr;
}

static jlong JNICALL kudroid_shim_GetDirectBufferCapacity(JNIEnv* env, jobject buf) {
    (void)env;
    if (!buf) return 0;
    std::lock_guard<std::mutex> lock(g_directBufferMutex);
    auto it = g_directBuffers.find(buf);
    if (it != g_directBuffers.end()) {
        return it->second.capacity;
    }
    return 0;
}

static void patch_jnienv_vtable(JNIEnv* env) {
    if (!env || !env->functions) return;
    if (env->functions == &g_interposedJniFunctions) return;

    if (!g_interposedFunctionsInitialized) {
        g_interposedJniFunctions = *(env->functions);
        g_interposedJniFunctions.NewDirectByteBuffer = kudroid_shim_NewDirectByteBuffer;
        g_interposedJniFunctions.GetDirectBufferAddress = kudroid_shim_GetDirectBufferAddress;
        g_interposedJniFunctions.GetDirectBufferCapacity = kudroid_shim_GetDirectBufferCapacity;
        g_interposedFunctionsInitialized = true;
    }
    const_cast<struct JNINativeInterface_*&>(env->functions) = &g_interposedJniFunctions;
}

static struct JNIInvokeInterface_ g_interposedVmFunctions;
static bool g_interposedVmInitialized = false;
static jint (*g_origAttachCurrentThread)(JavaVM*, void**, void*) = nullptr;
static jint (*g_origGetEnv)(JavaVM*, void**, jint) = nullptr;

static jint JNICALL kudroid_shim_AttachCurrentThread(JavaVM* vm, void** penv, void* args) {
    if (!vm || !g_origAttachCurrentThread) return JNI_ERR;
    jint res = g_origAttachCurrentThread(vm, penv, args);
    if (res == JNI_OK && penv && *penv) {
        patch_jnienv_vtable(static_cast<JNIEnv*>(*penv));
    }
    return res;
}

static jint JNICALL kudroid_shim_GetEnv(JavaVM* vm, void** penv, jint version) {
    if (!vm || !g_origGetEnv) return JNI_ERR;
    jint res = g_origGetEnv(vm, penv, version);
    if (res == JNI_OK && penv && *penv) {
        patch_jnienv_vtable(static_cast<JNIEnv*>(*penv));
    }
    return res;
}

static void patch_javavm_vtable(JavaVM* vm) {
    if (!vm || !vm->functions) return;
    if (vm->functions == &g_interposedVmFunctions) return;

    if (!g_interposedVmInitialized) {
        g_origAttachCurrentThread = vm->functions->AttachCurrentThread;
        g_origGetEnv = vm->functions->GetEnv;

        g_interposedVmFunctions = *(vm->functions);
        g_interposedVmFunctions.AttachCurrentThread = kudroid_shim_AttachCurrentThread;
        g_interposedVmFunctions.GetEnv = kudroid_shim_GetEnv;
        g_interposedVmInitialized = true;
    }
    const_cast<struct JNIInvokeInterface_*&>(vm->functions) = &g_interposedVmFunctions;
}

static void log_jni(const char* fmt, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    fprintf(stderr, "[kudroid_jni] %s\n", buffer);

    // Mirror REAL-TIME vào crash buffer (qua kudroid_android_log_message —
    // pipeline chuẩn: stdout + kudroid_android_logs.txt + crash buffer). Trước
    // đây log_jni chỉ ghi stderr + callback (callback chỉ append vào string test,
    // không mirror) → crash giữa JVM init (vd JNI_CreateJavaVM) làm "log up to
    // crash" dừng trước "Initializing Avian JVM..." dù code đã chạy tới đó.
    kudroid_android_log_message(2, "kudroid_jni", buffer);

    std::lock_guard<std::mutex> lock(g_log_mutex);
    if (g_jni_log_callback) {
        g_jni_log_callback(buffer);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// đường dẫn lớp khởi động được nhúng
//
// đường dẫn lớp khởi động avian được cung cấp dưới dạng một tệp jar được nhúng. bản dựng mặc định
// của avian (không có hình ảnh khởi động) nhúng tệp jar đường dẫn lớp thông qua đối tượng `classpath-jar.o`
// (được xây dựng từ một đoạn mã hợp ngữ .incbin bởi scripts/patch-avian-ios.py)
// và hiển thị nó thông qua ký hiệu `classpathjar()` trong boot.cpp. chúng tôi chọn nó
// với tùy chọn "-xbootclasspath:[classpathjar]".
//
// các lớp khung (android.*) được hợp nhất vào cùng tệp jar đường dẫn lớp đó bởi
// tệp makefile avian đã được vá, vì vậy chúng có sẵn trên đường dẫn lớp khởi động.
// ─────────────────────────────────────────────────────────────────────────────

#if (defined __MINGW32__) || (defined _MSC_VER)
#  define KUDROID_EXPORT __declspec(dllexport)
#else
#  define KUDROID_EXPORT __attribute__ ((visibility("default"))) __attribute__ ((used))
#endif

// Lưu ý: không định nghĩa __cxa_pure_virtual ở đây — boot.cpp của avian (được link
// qua libavian.a) đã định nghĩa nó. Nếu khai báo trùng ở đây sẽ báo duplicate symbol
// khi linker kéo boot.o vào (do tham chiếu classpathJar).

// ─────────────────────────────────────────────────────────────────────────────
// phương thức vòng đời jvm
// ─────────────────────────────────────────────────────────────────────────────

// classpath ứng dụng đã áp dụng cho VM hiện tại (so sánh để biết có cần
// re-init khi AOT jar khác — Avian không có append classpath lúc runtime).
static std::string g_appClasspath;

void kudroid_jni_init_jvm(const char* bootclasspath, const char* classpath) {
    (void)bootclasspath; // avian luôn sử dụng tệp jar đường dẫn lớp được nhúng
    std::lock_guard<std::mutex> lock(g_jvm_mutex);

    if (g_vm) {
        // VM đã tồn tại (có thể từ run trước trong cùng process, init với
        // classpath rỗng vì AOT chưa sẵn). Nếu run này có app classpath KHÁC
        // thì phá VM tạo lại — nếu không, mọi FindClass class ứng dụng fail
        // → fbjni ném JniException → populateWhat đệ quy → crash/hang.
        if (classpath && classpath[0] != '\0' && g_appClasspath != classpath) {
            log_jni("Re-initializing JVM with app classpath: %s", classpath);
            if (g_vm->DestroyJavaVM() != 0) {
                log_jni("WARNING: DestroyJavaVM returned non-zero");
            }
            g_vm = nullptr;
            g_env = nullptr;
        } else {
            return; // đã được khởi tạo với classpath phù hợp
        }
    }
    g_appClasspath = classpath ? classpath : "";

    // Build stamp (commit hash) ở dòng đầu của log JVM — phân biệt bản IPA cũ/mới
    // ngay cả khi chỉ nhìn đoạn log JVM init.
    log_jni("Build: %s", kudroid_build_stamp());
    log_jni("Initializing Avian JVM...");

    // Ép linker giữ boot.o + classpath-jar.o và log kích thước boot jar nhúng
    // (classpathJar được định nghĩa trong boot.cpp của avian).
    {
        size_t bootJarSize = 0;
        const uint8_t* bootJarData = classpathJar(&bootJarSize);
        if (bootJarData && bootJarSize >= 4) {
            log_jni("Embedded boot classpath jar: %zu bytes (header: 0x%02X 0x%02X 0x%02X 0x%02X '%c%c%c%c')",
                    bootJarSize,
                    bootJarData[0], bootJarData[1], bootJarData[2], bootJarData[3],
                    (bootJarData[0] >= 32 && bootJarData[0] < 127) ? bootJarData[0] : '.',
                    (bootJarData[1] >= 32 && bootJarData[1] < 127) ? bootJarData[1] : '.',
                    (bootJarData[2] >= 32 && bootJarData[2] < 127) ? bootJarData[2] : '.',
                    (bootJarData[3] >= 32 && bootJarData[3] < 127) ? bootJarData[3] : '.');
        } else {
            log_jni("Embedded boot classpath jar: %zu bytes (MISSING/EMPTY)", bootJarSize);
        }
    }

    // xây dựng các đối số máy ảo. boot classpath được nhúng (classpathJar()),
    // NHƯNG phải được dùng dưới dạng FILE THẬT — không phải placeholder
    // "-Xbootclasspath:[classpathJar]". Placeholder được Avian xử lý như
    // BuiltinElement: dlopen(main executable) + dlsym("classpathJar"). Nếu
    // symbol không resolve được trên máy thật → boot finder RỖNG →
    // java/lang/Object NOT FOUND → MỌI class load fail (kể cả class trong
    // app jar dù -Djava.class.path đúng). Tái hiện + chứng minh trên Linux:
    //   placeholder+cp  → String/T/Object NOT FOUND  ✗
    //   bootJAR file+cp → String/T/Object FOUND      ✓
    // Vì vậy: ghi embedded boot jar ra file thật và dùng đường dẫn đó để boot finder luôn có file thật.
    std::string bootOption = "-Xbootclasspath:[classpathJar]";
    std::string bootJarFile;
    if (classpath && classpath[0] != '\0') {
        const char* lastSlash = strrchr(classpath, '/');
        const std::string dir = lastSlash
            ? std::string(classpath, (size_t)(lastSlash - classpath + 1))
            : std::string();
        bootJarFile = dir + "boot.jar";
    } else {
        auto& remapper = kudroid::VFSPathRemapper::getInstance();
        std::string root = remapper.androidRoot();
        if (root.empty()) root = "/tmp";
        bootJarFile = root + "/boot.jar";
    }

    size_t bootJarSize = 0;
    const uint8_t* bootJarData = classpathJar(&bootJarSize);
    if (bootJarData && bootJarSize && !bootJarFile.empty()) {
        FILE* f = fopen(bootJarFile.c_str(), "wb");
        if (f) {
            size_t written = fwrite(bootJarData, 1, bootJarSize, f);
            fclose(f);
            if (written == bootJarSize) {
                log_jni("Boot classpath jar materialized: %s (%zu bytes)",
                        bootJarFile.c_str(), bootJarSize);
            }
        }
    }

    char realBuf[PATH_MAX];
    if (::realpath(bootJarFile.c_str(), realBuf)) {
        bootJarFile = realBuf;
    }
    struct stat stBoot;
    if (::stat(bootJarFile.c_str(), &stBoot) == 0) {
        log_jni("boot.jar ready (canonical): %s (%lld bytes)", bootJarFile.c_str(), (long long)stBoot.st_size);
    } else {
        log_jni("ERROR: boot.jar stat failed: %s (errno=%d)", bootJarFile.c_str(), errno);
    }

    std::string frameworkJarFile;
    if (classpath && classpath[0] != '\0') {
        std::string cp(classpath);
        auto slash = cp.rfind('/');
        std::string dir = (slash != std::string::npos) ? cp.substr(0, slash + 1) : "";
        frameworkJarFile = dir + "framework.jar";
    }
    if (!frameworkJarFile.empty()) {
        FILE* ff = fopen(frameworkJarFile.c_str(), "wb");
        if (ff) {
            fwrite(g_framework_jar_bytes, 1, g_framework_jar_size, ff);
            fclose(ff);
        }
        if (::realpath(frameworkJarFile.c_str(), realBuf)) {
            frameworkJarFile = realBuf;
        }
        struct stat stFw;
        if (::stat(frameworkJarFile.c_str(), &stFw) == 0) {
            log_jni("framework.jar ready (canonical): %s (%lld bytes)", frameworkJarFile.c_str(), (long long)stFw.st_size);
        }
    }

    // Sử dụng trực tiếp built-in boot classpath [classpathJar] đã được link tĩnh vào binary qua symbol _classpathJar
    bootOption = "-Xbootclasspath:[classpathJar]";

    std::string canonicalClasspath = classpath ? classpath : "";
    if (!canonicalClasspath.empty() && ::realpath(canonicalClasspath.c_str(), realBuf)) {
        canonicalClasspath = realBuf;
    }

    // ── AUTO-STUB ─────────────────────────────────────────────────────────
    // Đọc classes.jar của app, tự tìm mọi class android/* mà app tham chiếu
    // nhưng framework chưa có, rồi sinh stub.jar. Nhờ vậy KHÔNG cần vá tay
    // từng class vào framework cho mọi app mới — JVM luôn resolve được
    // hierarchy. Gọi method thật trên stub → NoSuchMethodError (lỗi rõ, chỉ
    // ảnh hưởng đúng method thiếu) thay vì ClassNotFoundException chặn cả
    // Activity.
    std::string stubJarFile;
    if (!canonicalClasspath.empty()) {
        auto slash2 = canonicalClasspath.rfind('/');
        std::string dir2 = (slash2 != std::string::npos)
                               ? canonicalClasspath.substr(0, slash2 + 1) : "";
        const std::string candidate = dir2 + "autostub.jar";
        const int stubs = kudroid::AutoStub::build_stub_jar(canonicalClasspath, candidate);
        if (stubs > 0) {
            if (::realpath(candidate.c_str(), realBuf)) {
                stubJarFile = realBuf;
            } else {
                stubJarFile = candidate;
            }
            log_jni("AutoStub: %d generated stub classes -> %s", stubs, stubJarFile.c_str());
        } else {
            log_jni("AutoStub: no missing android/* classes (or jar unreadable)");
        }
    }

    std::string classpathOption;
    std::string bootAppendOption;
    if (!canonicalClasspath.empty()) {
        // Thứ tự: app jar → framework (stub tay, ưu tiên cao hơn autostub)
        // → autostub (sinh tự động, chỉ dùng khi hai cái trên không có).
        std::string extra;
        if (!frameworkJarFile.empty()) extra += ":" + frameworkJarFile;
        if (!stubJarFile.empty()) extra += ":" + stubJarFile;
        classpathOption = std::string("-Djava.class.path=") + canonicalClasspath + extra;
        std::string bootExtra;
        if (!frameworkJarFile.empty()) bootExtra += frameworkJarFile + ":";
        if (!stubJarFile.empty()) bootExtra += stubJarFile + ":";
        bootAppendOption = std::string("-Xbootclasspath/a:") + bootExtra + canonicalClasspath;
        struct stat st;
        if (::stat(canonicalClasspath.c_str(), &st) == 0) {
            log_jni("App classpath jar exists (canonical): %s (%lld bytes)", canonicalClasspath.c_str(),
                    (long long)st.st_size);
        } else {
            log_jni("ERROR: App classpath jar MISSING/UNREADABLE: %s", canonicalClasspath.c_str());
        }
    }
    log_jni("JVM options: %s%s%s", bootOption.c_str(),
            classpathOption.empty() ? "" : (" " + classpathOption).c_str(),
            bootAppendOption.empty() ? "" : (" " + bootAppendOption).c_str());

    // giới hạn vùng nhớ heap ở một kích thước hợp lý cho ios (tránh áp lực bộ nhớ).
    // avian chấp nhận -xmx<n>m.
    std::string heapOption = "-Xmx256m";

    // đếm số tùy chọn: tệp jar đường dẫn lớp + vùng nhớ heap luôn hiện diện;
    // java.class.path và bootclasspath/a là tùy chọn (có khi có AOT jar).
    int nOptions = 2;
    if (!classpathOption.empty()) nOptions++;
    if (!bootAppendOption.empty()) nOptions++;

    JavaVMOption options[4];
    options[0].optionString = const_cast<char*>(bootOption.c_str());
    options[1].optionString = const_cast<char*>(heapOption.c_str());
    int oi = 2;
    if (!classpathOption.empty()) {
        options[oi++].optionString = const_cast<char*>(classpathOption.c_str());
    }
    if (!bootAppendOption.empty()) {
        options[oi++].optionString = const_cast<char*>(bootAppendOption.c_str());
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
    patch_jnienv_vtable(g_env);
    patch_javavm_vtable(g_vm);
    log_jni("Avian JVM initialized successfully (JavaVM=%p, JNIEnv=%p, init_thread=%p)",
            (void*)g_vm, (void*)g_env, (void*)pthread_self());

    // DIRECT PROBE trên đúng thread khởi tạo JVM:
    log_jni("[DirectProbe] Calling FindClass('java/lang/String') on init_thread %p...", (void*)pthread_self());
    jclass probeStr = g_env->FindClass("java/lang/String");
    if (probeStr) {
        log_jni("[DirectProbe] -> SUCCESS: Found java/lang/String (jclass=%p)", (void*)probeStr);
        g_env->DeleteLocalRef(probeStr);
    } else {
        log_jni("[DirectProbe] -> FAILED: java/lang/String returned NULL!");
        if (g_env->ExceptionCheck()) g_env->ExceptionClear();
    }

    // Đăng ký native method của framework ngay tại đây — nếu bỏ lỡ, Java gọi
    // System.arraycopy hoặc Log.* sẽ UnsatisfiedLinkError.
    register_java_lang_system_natives(g_env);
    register_android_util_log_natives(g_env);
    register_android_graphics_canvas_natives(g_env);

    // Đẩy kích thước màn hình thật (từ UIScreen qua kudroid_set_metal_layer)
    // vào DisplayMetrics — game đọc Resources.getDisplayMetrics() sẽ thấy số
    // liệu chuẩn xác tới từng pixel thay vì hardcode 1080x1920.
    kudroid_jni_update_display_metrics();
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
    kudroid_jni_init_jvm("", "");
    std::lock_guard<std::mutex> lock(g_jvm_mutex);
    return g_vm;
}

// Kiểm tra class có extends android.app.Activity không — dùng JNI IsAssignableFrom
// nên chính xác kể cả class bị ProGuard obfuscate (a.a.a v.v.). Đây là cách duy
// nhất đáng tin khi manifest không parse được: tên class không nói lên gì.
extern "C" int kudroid_class_extends_activity(const char* className) {
    std::lock_guard<std::mutex> lock(g_jvm_mutex);
    if (!g_vm || !g_env || !className || !*className) return 0;

    // Cache android/app/Activity bằng GlobalRef — FindClass mỗi lần là một
    // lookup đầy đủ trong JVM; vòng launcher-verify có thể gọi hàng nghìn
    // lần liên tiếp. Mutex ở trên đảm bảo init an toàn giữa các thread.
    static jclass cachedActivity = nullptr;
    if (!cachedActivity) {
        jclass local = g_env->FindClass("android/app/Activity");
        if (!local) {
            if (g_env->ExceptionCheck()) g_env->ExceptionClear();
            return 0;
        }
        cachedActivity = static_cast<jclass>(g_env->NewGlobalRef(local));
        g_env->DeleteLocalRef(local);
    }

    // Đổi dot → slash cho FindClass ("com.foo.Bar" → "com/foo/Bar").
    std::string slashed(className);
    for (char& c : slashed) if (c == '.') c = '/';

    jclass target = g_env->FindClass(slashed.c_str());
    if (!target) {
        if (g_env->ExceptionCheck()) g_env->ExceptionClear();
        return 0;
    }
    const jboolean isActivity =
        g_env->IsAssignableFrom(target, cachedActivity);
    g_env->DeleteLocalRef(target);
    return isActivity == JNI_TRUE ? 1 : 0;
}

extern "C" jint JNI_GetCreatedJavaVMs(JavaVM** vmBuf, jsize bufLen, jsize* nVMs) {
    JavaVM* vm = kudroid_jni_get_javavm();
    if (nVMs) *nVMs = vm ? 1 : 0;
    if (vmBuf && bufLen > 0 && vm) {
        vmBuf[0] = vm;
        return JNI_OK;
    }
    return JNI_OK;
}

jint kudroid_jni_get_env(JavaVM* vm, void** env, jint version) {
    (void)vm;
    (void)version;
    kudroid_jni_init_jvm("", ""); // khởi tạo nếu chưa có

    std::lock_guard<std::mutex> lock(g_jvm_mutex);
    if (!g_vm || !env) {
        return JNI_ERR;
    }

    // jnienv là trên mỗi luồng trong một jvm thực. nếu luồng đang gọi không phải là
    // luồng đã tạo máy ảo, hãy gắn nó để lấy jnienv của riêng nó.
    JNIEnv* threadEnv = nullptr;
    jint status = g_vm->GetEnv(reinterpret_cast<void**>(&threadEnv), JNI_VERSION_1_6);
    if (status == JNI_OK && threadEnv) {
        // Touch/vuốt gọi hàm này ~60 lần/giây (InputShim → postTouchEvent) → log
        // mỗi lần sẽ nhấn chìm mọi dòng khác. Chỉ log lần đầu trên mỗi thread.
        static thread_local bool loggedAttached = false;
        if (!loggedAttached) {
            loggedAttached = true;
            log_jni("kudroid_jni_get_env: Thread already attached (current_thread=%p), returning env=%p",
                    (void*)pthread_self(), (void*)threadEnv);
        }
        *env = threadEnv;
        return JNI_OK;
    }
    if (status == JNI_EDETACHED) {
        log_jni("kudroid_jni_get_env: Thread detached (current_thread=%p), attempting to AttachCurrentThread...",
                (void*)pthread_self());
        // đính kèm luồng này vào máy ảo.
        status = g_vm->AttachCurrentThread(reinterpret_cast<void**>(&threadEnv), nullptr);
        if (status == JNI_OK && threadEnv) {
            log_jni("kudroid_jni_get_env: Thread attached successfully, env=%p", (void*)threadEnv);
            // Nhớ DetachCurrentThread khi thread kết thúc (chỉ cho thread do TA gắn).
            ::pthread_once(&g_jni_attach_once, init_jni_attach_key);
            ::pthread_setspecific(g_jni_attach_key, reinterpret_cast<void*>(1));
            *env = threadEnv;
            return JNI_OK;
        } else {
            log_jni("ERROR: kudroid_jni_get_env: AttachCurrentThread failed with code %d", status);
        }
    } else {
        log_jni("ERROR: kudroid_jni_get_env: GetEnv failed with code %d", status);
    }
    // KHÔNG fallback về env của thread chính — dùng JNIEnv của thread khác là
    // hành vi không xác định trong JNI, có thể crash JVM.
    return JNI_ERR;
}

// Cập nhật DisplayMetrics trong Java với số liệu màn hình thật. Chỉ gọi khi
// đang ở trên một thread đã attach (init_jvm gọi ngay sau khi tạo VM).
//
// ═══ CHẨN ĐOÁN 2026-08-12 — BỆNH ĐÃ XÁC ĐỊNH ═══════════════════════════════
// Cả test_jni_massive lẫn test_triangle crash SIGABRT tại ĐÚNG điểm này: log
// dừng sau "Registered android/util/Log.println_native" (bước kế tiếp duy nhất
// là hàm này).
//
// Nguyên nhân: DisplayMetrics là class Java ĐẦU TIÊN dùng FLOAT được JIT-compile
// trên arm64 — <clinit> có float statics (sDensity=3.0f...), updateFromNative(IIF)V
// có param float. arm64 codegen của Avian abort() câm (không in lý do) khi gặp
// op float — cùng họ bug UB/unsupported-op với c->saved->count() đã fix, nhưng
// lần này nằm trong target arm64.
//
// Bằng chứng: (1) JVM init OK — String/Object/Log (không float) compile tốt;
// (2) repro Linux x86_64 chạy ĐÚNG chuỗi JNI này (FindClass+CallStaticVoidMethod
// trên cùng jar 1100849 bytes) qua CẢ GCC lẫn clang ở -O3 JIT mode → chỉ arm64
// chết; (3) abort câm (x6=0xffffffff, không abort message) = expect()/abort(c)
// trong compiler, không phải exception bắt được.
//
// FIX tạm: bỏ qua bước này — game test (triangle) không đọc DisplayMetrics.
// Khi arm64 codegen của Avian được fix (patch qua scripts/patch-avian-ios.py),
// xóa khối #if 0 này để đẩy số liệu màn hình thật vào Java lại.
// ════════════════════════════════════════════════════════════════════════════
extern "C" void kudroid_jni_update_display_metrics(void) {
    log_jni("DisplayMetrics update DISABLED: arm64 JIT float codegen aborts on this "
            "class (see comment above); screen dims left at defaults");
    return;
#if 0  // bị vô hiệu hóa tạm thời — xem comment chẩn đoán phía trên
    if (!g_vm || !g_env) return;
    jclass clazz = g_env->FindClass("android/util/DisplayMetrics");
    if (!clazz) {
        if (g_env->ExceptionCheck()) g_env->ExceptionClear();
        log_jni("WARNING: FindClass(android/util/DisplayMetrics) failed");
        return;
    }
    jmethodID update = g_env->GetStaticMethodID(clazz, "updateFromNative", "(IIF)V");
    if (update) {
        g_env->CallStaticVoidMethod(clazz, update,
                                    g_metalLayerWidth, g_metalLayerHeight,
                                    g_metalLayerDensity);
        log_jni("DisplayMetrics updated: %dx%d density=%.2f",
                g_metalLayerWidth, g_metalLayerHeight, g_metalLayerDensity);
    } else {
        log_jni("WARNING: updateFromNative(IIIF)V not found in DisplayMetrics");
    }
    if (g_env->ExceptionCheck()) g_env->ExceptionClear();
    g_env->DeleteLocalRef(clazz);
#endif  // #if 0 — DisplayMetrics update bị vô hiệu hóa tạm thời
}
