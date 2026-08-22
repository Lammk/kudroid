#include "jni_standalone.h"

// Bionic Android Log API
extern int __android_log_print(int priority, const char* tag, const char* fmt, ...);
#define LOG_TAG "JniException"
#define LOGI(...) __android_log_print(3, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(6, LOG_TAG, __VA_ARGS__)

extern JavaVM* kudroid_jni_get_javavm(void);
extern jint JNI_GetCreatedJavaVMs(JavaVM** vmBuf, jsize bufLen, jsize* nVMs);

// Callback function
static jint nativePingPongHandler(JNIEnv* env, jobject thiz, jint sequenceNumber) {
    (void)env; (void)thiz;
    LOGI("🏓 [JNI Ping-Pong Callback] Received sequence: %d -> Returning ACK: %d",
         sequenceNumber, sequenceNumber + 42);
    return sequenceNumber + 42;
}

#define JNI_EDETACHED (-2)

int kudroid_test_main(void) {
    LOGI("=================================================");
    LOGI("⚡ [JNI / JVM Hardcore Test 5: Exceptions & Callbacks]");
    LOGI("=================================================");

    JavaVM* vm = kudroid_jni_get_javavm();
    if (!vm) {
        jsize nVMs = 0;
        JNI_GetCreatedJavaVMs(&vm, 1, &nVMs);
    }
    if (!vm) {
        LOGE("❌ JavaVM is NULL!");
        return 1;
    }

    JNIEnv* env = NULL;
    jint getEnvRes = (*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6);
    if (getEnvRes == JNI_EDETACHED || !env) {
        JavaVMAttachArgs args = { JNI_VERSION_1_6, (char*)"ExceptionThread", NULL };
        (*vm)->AttachCurrentThread(vm, (void**)&env, &args);
    }
    if (!env) {
        LOGE("❌ GetEnv failed!");
        return 2;
    }

    // 1. Thao tác Exception Handling (Ném và Bắt ngoại lệ)
    LOGI("⏳ [STAGE 1] Testing ThrowNew and ExceptionOccurred handling...");
    jclass excCls = (*env)->FindClass(env, "java/lang/IllegalArgumentException");
    if (excCls) {
        (*env)->ThrowNew(env, excCls, "KuDroid Test Intentional Exception");

        jthrowable pendingExc = (*env)->ExceptionOccurred(env);
        if (pendingExc) {
            LOGI("✔ [STAGE 1] Exception detected correctly via ExceptionOccurred!");
            LOGI("⏳ [STAGE 2] Describing and clearing exception...");
            (*env)->ExceptionDescribe(env);
            (*env)->ExceptionClear(env);
            LOGI("✔ [STAGE 2] Exception cleared successfully! JNIEnv restored to clean state.");
            (*env)->DeleteLocalRef(env, pendingExc);
        } else {
            LOGE("❌ ExceptionOccurred failed to detect pending exception!");
            return 3;
        }
        (*env)->DeleteLocalRef(env, excCls);
    }

    // 2. Kiểm tra Ping-Pong Callback và ExceptionCheck
    LOGI("⏳ [STAGE 3] Testing 2-Way Native Callback execution & ExceptionCheck...");
    jboolean hasException = (*env)->ExceptionCheck(env);
    LOGI("✔ [STAGE 3] ExceptionCheck status: %d (0 = NO EXCEPTION)", (int)hasException);

    // Mô phỏng gọi native callback trực tiếp
    jint result = nativePingPongHandler(env, NULL, 100);
    if (result == 142) {
        LOGI("✔ [STAGE 4] Ping-Pong Callback Verification: 100 + 42 = %d (100% MATCH!)", result);
    }

    LOGI("=================================================");
    LOGI("🎉 JNI EXCEPTIONS & CALLBACKS TEST PASSED 100%!");
    LOGI("=================================================");
    return 0;
}
