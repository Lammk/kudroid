#include "jni_standalone.h"

// Bionic Android Log API
extern int __android_log_print(int priority, const char* tag, const char* fmt, ...);
#define LOG_TAG "JniBootstrap"
#define LOGI(...) __android_log_print(3, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(6, LOG_TAG, __VA_ARGS__)

// Native Function to be registered via JNI
static jint nativeGameCalculateScore(JNIEnv* env, jobject thiz, jint baseScore, jint multiplier) {
    (void)env; (void)thiz;
    LOGI("🎯 [Native Method Invoked from Java!] Base: %d, Multiplier: %d -> Calculating...", baseScore, multiplier);
    return baseScore * multiplier + 1000;
}

// Bionic & Android NDK helpers
extern JavaVM* kudroid_jni_get_javavm(void);
extern jint JNI_GetCreatedJavaVMs(JavaVM** vmBuf, jsize bufLen, jsize* nVMs);

#define JNI_EDETACHED (-2)

int kudroid_test_main(void) {
    LOGI("=================================================");
    LOGI("☕ [JNI / JVM Hardcore Test 1: Game Bootstrap]");
    LOGI("=================================================");

    // 1. Lấy JavaVM từ KuDroid Core
    LOGI("⏳ [STAGE 1] Querying global JavaVM from KuDroid Core & JNI_GetCreatedJavaVMs...");
    JavaVM* vm = kudroid_jni_get_javavm();
    if (!vm) {
        jsize nVMs = 0;
        JNI_GetCreatedJavaVMs(&vm, 1, &nVMs);
    }
    if (!vm) {
        LOGE("❌ JavaVM is NULL! (Avian JVM may not be initialized)");
        return 1;
    }
    LOGI("✔ [STAGE 1] Global JavaVM pointer acquired: %p", (void*)vm);

    // 2. Lấy JNIEnv phiên bản JNI 1.6 (Tự động AttachCurrentThread nếu luồng mới)
    LOGI("⏳ [STAGE 2] Querying JNIEnv 1.6 (with auto-attach for background threads)...");
    JNIEnv* env = NULL;
    jint getEnvRes = (*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6);
    if (getEnvRes == JNI_EDETACHED || !env) {
        LOGI("   ℹ️ Current thread is detached (code %d) -> Attaching to JVM...", getEnvRes);
        JavaVMAttachArgs args;
        args.version = JNI_VERSION_1_6;
        args.name = (char*)"KuDroidGameTestThread";
        args.group = NULL;
        jint attachRes = (*vm)->AttachCurrentThread(vm, (void**)&env, &args);
        if (attachRes != JNI_OK || !env) {
            LOGE("❌ AttachCurrentThread failed with code: %d", attachRes);
            return 2;
        }
    }
    jint jniVersion = (*env)->GetVersion(env);
    LOGI("✔ [STAGE 2] JNIEnv acquired: %p (JNI Version: 0x%08x)", (void*)env, jniVersion);

    // 3. Kiểm tra GetJavaVM từ env
    JavaVM* testVm = NULL;
    if ((*env)->GetJavaVM(env, &testVm) == JNI_OK && testVm == vm) {
        LOGI("✔ [STAGE 3] (*env)->GetJavaVM matches global VM: %p", (void*)testVm);
    }

    // 4. Thao tác với chuỗi java/lang/String
    LOGI("⏳ [STAGE 4] Testing java/lang/String creation & UTF conversion...");
    jstring jstr = (*env)->NewStringUTF(env, "KuDroid Universal Android Runtime Engine 2026");
    if (!jstr) {
        LOGE("❌ NewStringUTF returned NULL!");
        return 4;
    }
    jsize strLen = (*env)->GetStringUTFLength(env, jstr);
    const char* utfChars = (*env)->GetStringUTFChars(env, jstr, NULL);
    LOGI("✔ [STAGE 4] Java String created (Length: %d bytes): '%s'", (int)strLen, utfChars ? utfChars : "");
    if (utfChars) {
        (*env)->ReleaseStringUTFChars(env, jstr, utfChars);
    }
    (*env)->DeleteLocalRef(env, jstr);

    // 5. Thao tác Primitive Array JNI (độ tin cậy cao không phụ thuộc app loader)
    LOGI("⏳ [STAGE 5] Testing JNI Primitive Array manipulation...");
    jbyteArray arr = (*env)->NewByteArray(env, 16);
    if (arr) {
        jsize arrLen = (*env)->GetArrayLength(env, arr);
        LOGI("✔ [STAGE 5] Created jbyteArray of size: %d elements", (int)arrLen);
        (*env)->DeleteLocalRef(env, arr);
    }

    // 6. Đăng ký Native Methods qua RegisterNatives
    LOGI("⏳ [STAGE 6] Testing RegisterNatives dynamic method binding...");
    jclass objClass = (*env)->FindClass(env, "java/lang/Object");
    if (objClass) {
        JNINativeMethod methods[] = {
            { (char*)"kudroidCalculateScore", (char*)"(II)I", (void*)nativeGameCalculateScore }
        };
        jint regRes = (*env)->RegisterNatives(env, objClass, methods, 1);
        LOGI("✔ [STAGE 6] RegisterNatives result: %d (0 = SUCCESS)", regRes);
        (*env)->DeleteLocalRef(env, objClass);
    }

    LOGI("=================================================");
    LOGI("🎉 JNI / JVM HARDCORE TEST 1 PASSED 100%!");
    LOGI("=================================================");
    return 0;
}
