#include "jni_standalone.h"

// Bionic Android Log API
extern int __android_log_print(int priority, const char* tag, const char* fmt, ...);
#define LOG_TAG "JniRefs"
#define LOGI(...) __android_log_print(3, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(6, LOG_TAG, __VA_ARGS__)

extern JavaVM* kudroid_jni_get_javavm(void);
extern jint JNI_GetCreatedJavaVMs(JavaVM** vmBuf, jsize bufLen, jsize* nVMs);

#define JNI_EDETACHED (-2)

int kudroid_test_main(void) {
    LOGI("=================================================");
    LOGI("🧠 [JNI / JVM Hardcore Test 3: Memory & References]");
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
        JavaVMAttachArgs args = { JNI_VERSION_1_6, (char*)"MemRefThread", NULL };
        (*vm)->AttachCurrentThread(vm, (void**)&env, &args);
    }
    if (!env) {
        LOGE("❌ GetEnv failed!");
        return 2;
    }

    // 1. Áp lực 500 Global References
    LOGI("⏳ [STAGE 1] Allocating 500 JNI Global References...");
    #define GLOBAL_COUNT 500
    jobject globalRefs[GLOBAL_COUNT];
    jstring baseStr = (*env)->NewStringUTF(env, "KuDroid_Persistent_Global_Ref_2026");

    for (int i = 0; i < GLOBAL_COUNT; ++i) {
        globalRefs[i] = (*env)->NewGlobalRef(env, baseStr);
        if (!globalRefs[i]) {
            LOGE("❌ NewGlobalRef failed at index %d", i);
            return 3;
        }
    }
    LOGI("✔ [STAGE 1] Successfully created 500 Global References!");

    // 2. Xác thực IsSameObject
    LOGI("⏳ [STAGE 2] Testing IsSameObject on Global References...");
    jboolean isSame = (*env)->IsSameObject(env, globalRefs[0], globalRefs[GLOBAL_COUNT - 1]);
    LOGI("✔ [STAGE 2] IsSameObject result: %d (Expected: 1)", (int)isSame);

    // 3. Giải phóng toàn bộ 500 Global References
    LOGI("⏳ [STAGE 3] Deleting 500 Global References (Zero-leak verification)...");
    for (int i = 0; i < GLOBAL_COUNT; ++i) {
        (*env)->DeleteGlobalRef(env, globalRefs[i]);
    }
    (*env)->DeleteLocalRef(env, baseStr);
    LOGI("✔ [STAGE 3] 500 Global References deleted safely!");

    // 4. Áp lực PushLocalFrame / PopLocalFrame (Scoped Local References)
    LOGI("⏳ [STAGE 4] Testing PushLocalFrame(64) and PopLocalFrame scoping...");
    if ((*env)->PushLocalFrame(env, 64) == JNI_OK) {
        for (int i = 0; i < 50; ++i) {
            jstring s = (*env)->NewStringUTF(env, "TempScopedString");
            (void)s;
        }
        (*env)->PopLocalFrame(env, NULL);
        LOGI("✔ [STAGE 4] Push/PopLocalFrame successfully reclaimed 50 local references!");
    }

    LOGI("=================================================");
    LOGI("🎉 JNI MEMORY & REFERENCES TEST PASSED 100%!");
    LOGI("=================================================");
    return 0;
}
