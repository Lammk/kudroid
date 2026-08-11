#include "kudroid/kudroid_jni.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <mutex>
#include <string>
#include <functional>

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

// trạng thái toàn cục
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

// hàm hỗ trợ để ghi nhật ký chi tiết (an toàn luồng).
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

extern "C" void __cxa_pure_virtual(void) { abort(); }

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
        *env = threadEnv;
        return JNI_OK;
    }
    if (status == JNI_EDETACHED) {
        // đính kèm luồng này vào máy ảo.
        status = g_vm->AttachCurrentThread(reinterpret_cast<void**>(&threadEnv), nullptr);
        if (status == JNI_OK && threadEnv) {
            *env = threadEnv;
            return JNI_OK;
        }
    }
    // quay lại môi trường chính (nỗ lực tốt nhất).
    if (g_env) {
        *env = g_env;
        return JNI_OK;
    }
    return JNI_ERR;
}
