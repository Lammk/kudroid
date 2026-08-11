#include "kudroid/kudroid_jni.h"
#include <cstdio>

// khởi chạy activity thông qua activitythread để có looper xử lý vòng đời
extern "C" void kudroid_launch_java_activity(JavaVM* vm, const char* activityName) {
    fprintf(stdout, "[KuDroidApp] kudroid_launch_java_activity: requesting launch for %s\n", activityName ? activityName : "NULL");
    if (!vm || !activityName) return;

    JNIEnv* env = nullptr;
    if (kudroid_jni_get_env(vm, (void**)&env, JNI_VERSION_1_6) != JNI_OK || !env) {
        fprintf(stderr, "[kudroid_bridge] lỗi lấy jnienv\n");
        return;
    }
    
    jclass atClass = env->FindClass("android/app/ActivityThread");
    if (!atClass) {
        fprintf(stderr, "[kudroid_bridge] không tìm thấy android/app/activitythread\n");
        if (env->ExceptionCheck()) env->ExceptionClear();
        return;
    }
    
    jmethodID mainMethod = env->GetStaticMethodID(atClass, "main", "([Ljava/lang/String;)V");
    if (!mainMethod) {
        fprintf(stderr, "[kudroid_bridge] không tìm thấy hàm main trong activitythread\n");
        return;
    }
    
    // truyền tên activity vào mảng tham số cho hàm main
    jclass stringClass = env->FindClass("java/lang/String");
    jobjectArray args = env->NewObjectArray(1, stringClass, nullptr);
    jstring arg0 = env->NewStringUTF(activityName);
    env->SetObjectArrayElement(args, 0, arg0);
    env->DeleteLocalRef(arg0);
    
    fprintf(stderr, "[kudroid_bridge] khởi động activitythread.main (sẽ chặn luồng)...\n");
    env->CallStaticVoidMethod(atClass, mainMethod, args);
    
    if (env->ExceptionCheck()) {
        fprintf(stderr, "[kudroid_bridge] ngoại lệ trong looper của activitythread\n");
        env->ExceptionDescribe();
        env->ExceptionClear();
    }
}

// gửi sự kiện vòng đời (pause, resume) vào luồng ui
extern "C" void kudroid_send_lifecycle_event(int eventType) {
    fprintf(stdout, "[KuDroidApp] kudroid_send_lifecycle_event: preparing to send event=%d\n", eventType);
    JavaVM* vm = kudroid_jni_get_javavm();
    if (!vm) {
        fprintf(stdout, "[KuDroidApp] kudroid_send_lifecycle_event: ERROR no JVM found!\n");
        return;
    }
    
    JNIEnv* env = nullptr;
    bool attached = false;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        if (vm->AttachCurrentThread((void**)&env, nullptr) != JNI_OK) return;
        attached = true;
    }
    
    jclass atClass = env->FindClass("android/app/ActivityThread");
    if (atClass) {
        jmethodID postMethod = env->GetStaticMethodID(atClass, "postLifecycleEvent", "(ILjava/lang/String;)V");
        if (postMethod) {
            fprintf(stdout, "[KuDroidApp] kudroid_send_lifecycle_event: successfully calling postLifecycleEvent(%d)\n", eventType);
            env->CallStaticVoidMethod(atClass, postMethod, eventType, nullptr);
        } else {
            fprintf(stdout, "[KuDroidApp] kudroid_send_lifecycle_event: ERROR could not find postLifecycleEvent method\n");
        }
    } else {
        fprintf(stdout, "[KuDroidApp] kudroid_send_lifecycle_event: ERROR could not find ActivityThread class\n");
    }
    
    if (attached) {
        vm->DetachCurrentThread();
    }
}
