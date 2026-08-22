#include "jni_standalone.h"

// Pthread Bionic definitions
typedef unsigned long pthread_t;
typedef struct { unsigned int flags; } pthread_attr_t;
extern int pthread_create(pthread_t* thread, const pthread_attr_t* attr, void* (*start_routine)(void*), void* arg);
extern int pthread_join(pthread_t thread, void** retval);

// Bionic Android Log API
extern int __android_log_print(int priority, const char* tag, const char* fmt, ...);
#define LOG_TAG "JniMultiThread"
#define LOGI(...) __android_log_print(3, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(6, LOG_TAG, __VA_ARGS__)

extern JavaVM* kudroid_jni_get_javavm(void);
extern jint JNI_GetCreatedJavaVMs(JavaVM** vmBuf, jsize bufLen, jsize* nVMs);

typedef struct WorkerContext {
    int threadId;
    const char* threadName;
    JavaVM* vm;
    int success;
} WorkerContext;

static void* workerThreadRoutine(void* arg) {
    WorkerContext* ctx = (WorkerContext*)arg;
    LOGI("🧵 [Thread #%d: %s] Started, attempting AttachCurrentThread...", ctx->threadId, ctx->threadName);

    JavaVMAttachArgs attachArgs;
    attachArgs.version = JNI_VERSION_1_6;
    attachArgs.name = (char*)ctx->threadName;
    attachArgs.group = NULL;

    JNIEnv* threadEnv = NULL;
    jint attachRes = (*ctx->vm)->AttachCurrentThread(ctx->vm, (void**)&threadEnv, &attachArgs);
    if (attachRes != JNI_OK || !threadEnv) {
        LOGE("❌ [Thread #%d] AttachCurrentThread failed: %d", ctx->threadId, attachRes);
        ctx->success = 0;
        return NULL;
    }

    LOGI("✔ [Thread #%d] Attached to JVM! Thread JNIEnv: %p", ctx->threadId, (void*)threadEnv);

    // Thao tác JNI bên trong luồng
    jstring testStr = (*threadEnv)->NewStringUTF(threadEnv, ctx->threadName);
    if (testStr) {
        const char* utf = (*threadEnv)->GetStringUTFChars(threadEnv, testStr, NULL);
        LOGI("   👉 [Thread #%d] Created & Read UTF String: '%s'", ctx->threadId, utf ? utf : "");
        if (utf) (*threadEnv)->ReleaseStringUTFChars(threadEnv, testStr, utf);
        (*threadEnv)->DeleteLocalRef(threadEnv, testStr);
    }

    jbyteArray testArr = (*threadEnv)->NewByteArray(threadEnv, 32);
    if (testArr) {
        jsize arrLen = (*threadEnv)->GetArrayLength(threadEnv, testArr);
        (void)arrLen;
        (*threadEnv)->DeleteLocalRef(threadEnv, testArr);
    }

    // Detach thread
    jint detachRes = (*ctx->vm)->DetachCurrentThread(ctx->vm);
    LOGI("✔ [Thread #%d] Detached from JVM cleanly (Result: %d)", ctx->threadId, detachRes);

    ctx->success = (detachRes == JNI_OK) ? 1 : 0;
    return NULL;
}

int kudroid_test_main(void) {
    LOGI("=================================================");
    LOGI("🧵 [JNI / JVM Hardcore Test 2: Multi-Thread Attach]");
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

    #define NUM_THREADS 4
    const char* threadNames[NUM_THREADS] = {
        "GameRenderThread",
        "OpenSLAudioThread",
        "AssetLoaderThread",
        "NetworkWorkerThread"
    };

    pthread_t threads[NUM_THREADS];
    WorkerContext contexts[NUM_THREADS];

    LOGI("⏳ Launching %d concurrent Game Worker Threads to attach/detach JNI...", NUM_THREADS);

    for (int i = 0; i < NUM_THREADS; ++i) {
        contexts[i].threadId = i + 1;
        contexts[i].threadName = threadNames[i];
        contexts[i].vm = vm;
        contexts[i].success = 0;

        int rc = pthread_create(&threads[i], NULL, workerThreadRoutine, &contexts[i]);
        if (rc != 0) {
            LOGE("❌ Failed to spawn thread #%d", i + 1);
            return 2;
        }
    }

    // Chờ cả 4 luồng hoàn tất
    int allSuccess = 1;
    for (int i = 0; i < NUM_THREADS; ++i) {
        pthread_join(threads[i], NULL);
        if (!contexts[i].success) allSuccess = 0;
    }

    if (allSuccess) {
        LOGI("=================================================");
        LOGI("🎉 JNI MULTI-THREAD ATTACH/DETACH PASSED 100%!");
        LOGI("=================================================");
        return 0;
    } else {
        LOGE("❌ Some worker threads failed JNI attach/detach!");
        return 3;
    }
}
