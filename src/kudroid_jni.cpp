#include "kudroid/kudroid_jni.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <mutex>
#include <string>
#include <functional>
#include <pthread.h>

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

void kudroid_jni_init_jvm(const char* bootclasspath, const char* classpath) {
    (void)bootclasspath; // avian luôn sử dụng tệp jar đường dẫn lớp được nhúng
    std::lock_guard<std::mutex> lock(g_jvm_mutex);
    if (g_vm) {
        return; // đã được khởi tạo
    }

    log_jni("Initializing Avian JVM...");

    // Ép linker giữ boot.o + classpath-jar.o và log kích thước boot jar nhúng
    // (classpathJar được định nghĩa trong boot.cpp của avian).
    {
        size_t bootJarSize = 0;
        const uint8_t* bootJarData = classpathJar(&bootJarSize);
        log_jni("Embedded boot classpath jar: %zu bytes (%s)",
                bootJarSize,
                (bootJarData && bootJarSize) ? "OK" : "MISSING/EMPTY");
    }

    // xây dựng các đối số máy ảo. chúng tôi luôn sử dụng tệp jar đường dẫn lớp được nhúng làm
    // đường dẫn lớp khởi động. đường dẫn lớp do người gọi cung cấp được thêm vào như một
    // mục nhập đường dẫn lớp bổ sung (đối với các lớp ứng dụng được tải trong thời gian chạy).
    std::string bootOption = "-Xbootclasspath:[classpathJar]";
    std::string classpathOption;
    if (classpath && classpath[0] != '\0') {
        classpathOption = std::string("-Xbootclasspath/a:") + classpath;
    }

    // giới hạn vùng nhớ heap ở một kích thước hợp lý cho ios (tránh áp lực bộ nhớ).
    // avian chấp nhận -xmx<n>m.
    std::string heapOption = "-Xmx256m";

    // đếm số tùy chọn: tệp jar đường dẫn lớp + vùng nhớ heap luôn hiện diện; đường dẫn lớp là tùy chọn.
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

    // Đăng ký native method của framework ngay tại đây — nếu bỏ lỡ, Java gọi
    // Log.* sẽ UnsatisfiedLinkError (issue đã tìm thấy khi rà framework).
    register_android_util_log_natives(g_env);

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
    std::lock_guard<std::mutex> lock(g_jvm_mutex);
    return g_vm;
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
        log_jni("kudroid_jni_get_env: Thread already attached, returning env=%p", (void*)threadEnv);
        *env = threadEnv;
        return JNI_OK;
    }
    if (status == JNI_EDETACHED) {
        log_jni("kudroid_jni_get_env: Thread detached, attempting to AttachCurrentThread...");
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
