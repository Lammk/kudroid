// Runtime KuART: điểm vào duy nhất mà phần còn lại của KuDroid dùng để chạy
// bytecode Java của app.
//
// Thay hoàn toàn đường cũ (dex2jar → classes.jar → Avian JVM). KuART nạp thẳng
// classes*.dex của APK cùng framework.dex nhúng trong binary, rồi thông dịch.
//
// API để dạng C để dùng được từ cả C++ và Swift/Objective-C của vỏ iOS.
#ifndef KUDROID_KUART_RUNTIME_H
#define KUDROID_KUART_RUNTIME_H

#include <jni.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Log của runtime đi qua callback này (bridge nối vào kudroid_android_log_message).
void kuart_set_log_callback(void (*cb)(const char* message));

// Hook tra symbol native của guest, dùng để liên kết method `native` trong DEX.
// Bridge trỏ nó vào LibraryManager. Không cài thì chỉ tra được symbol của host.
void kuart_set_symbol_lookup(void* (*fn)(const char* symbol));

// Nạp framework.dex (nhúng) + mọi classes*.dex trong `app_dir`. Gọi lại khi đã
// khởi tạo thì không làm gì. Trả 1 nếu thành công.
// `app_dir` rỗng/NULL = chỉ nạp framework (dùng cho self-test).
int kuart_init(const char* app_dir);

void kuart_shutdown(void);

// 1 nếu kuart_init đã thành công.
int kuart_is_ready(void);

// JavaVM/JNIEnv để mã native của game gọi vào (JNI_OnLoad, RegisterNatives...).
JavaVM* kuart_get_javavm(void);
jint kuart_get_env(JavaVM* vm, void** env, jint version);

// Số DEX đã nạp và số class đã resolve — để log chẩn đoán.
size_t kuart_num_dex_files(void);
size_t kuart_num_loaded_classes(void);

// `class_name` dạng dấu chấm ("com.foo.Bar") hoặc dấu gạch chéo. Trả 1 nếu class
// thật sự kế thừa android.app.Activity — đúng cả khi ProGuard đổi tên thành a.a.a.
int kuart_class_extends_activity(const char* class_name);

// Liệt kê class của app (không phải framework/SDK) vào `out`, mỗi tên dạng
// "com/foo/Bar". Trả số phần tử đã ghi. Thay cho DexAotCache::list_app_classes.
size_t kuart_list_app_classes(char** out, size_t max_out);
void kuart_free_class_list(char** list, size_t count);

// Gọi ActivityThread.main(new String[]{activity_name, extra...}) — vào vòng lặp
// UI và không trả về cho tới khi Java thoát.
int kuart_launch_activity(const char* activity_name, const char* const* extra_candidates,
                          int extra_count);

// Đẩy sự kiện vòng đời (ActivityThread.postLifecycleEvent).
void kuart_send_lifecycle_event(int event_type);

// Đẩy sự kiện chạm (ActivityThread.postTouchEvent).
void kuart_post_touch_event(int action, float x, float y);

const char* kuart_last_error(void);

#ifdef __cplusplus
}
#endif

#endif  // KUDROID_KUART_RUNTIME_H
