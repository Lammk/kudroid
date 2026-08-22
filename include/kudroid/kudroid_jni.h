#ifndef KUDROID_JNI_H
#define KUDROID_JNI_H

#include <jni.h>

#ifdef __cplusplus
extern "C" {
#endif

// khởi tạo ánh xạ jvm và trả về con trỏ javavm toàn cục.
// nếu jvm chưa được khởi tạo, nó sẽ được khởi tạo ở đây.
jint kudroid_jni_get_env(JavaVM* vm, void** env, jint version);

// tạo và khởi tạo phiên bản minijvm nếu nó chưa được thực hiện.
void kudroid_jni_init_jvm(const char* bootclasspath, const char* classpath);

// phá hủy phiên bản minijvm và dọn dẹp.
void kudroid_jni_destroy_jvm(void);

// lấy phiên bản javavm giả toàn cục được kudroid sử dụng.
JavaVM* kudroid_jni_get_javavm(void);

// Kiểm tra class `className` (dạng dot, vd "com.foo.Bar") có THẬT SỰ kế thừa
// android.app.Activity hay không — dùng JNI AssignableFrom nên chính xác 100%
// kể cả class bị ProGuard obfuscate thành a.a.a. Trả về 1 nếu đúng, 0 nếu
// không/không tìm thấy/JVM chưa init.
int kudroid_class_extends_activity(const char* className);

#ifdef __cplusplus
}
#endif

#endif // KUDROID_JNI_H
