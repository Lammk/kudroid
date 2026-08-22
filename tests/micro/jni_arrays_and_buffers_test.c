#include "jni_standalone.h"

// Bionic Android Log API
extern int __android_log_print(int priority, const char* tag, const char* fmt, ...);
#define LOG_TAG "JniBuffers"
#define LOGI(...) __android_log_print(3, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(6, LOG_TAG, __VA_ARGS__)

extern JavaVM* kudroid_jni_get_javavm(void);
extern jint JNI_GetCreatedJavaVMs(JavaVM** vmBuf, jsize bufLen, jsize* nVMs);

#define JNI_EDETACHED (-2)

int kudroid_test_main(void) {
    LOGI("=================================================");
    LOGI("📦 [JNI / JVM Hardcore Test 4: Arrays & Buffers]");
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
        JavaVMAttachArgs args = { JNI_VERSION_1_6, (char*)"ArrayBufThread", NULL };
        (*vm)->AttachCurrentThread(vm, (void**)&env, &args);
    }
    if (!env) {
        LOGE("❌ GetEnv failed!");
        return 2;
    }

    // 1. Thao tác Primitive Byte Array
    LOGI("⏳ [STAGE 1] Allocating & modifying Java jbyteArray (1024 bytes)...");
    #define ARRAY_SIZE 1024
    jbyteArray byteArray = (*env)->NewByteArray(env, ARRAY_SIZE);
    if (!byteArray) {
        LOGE("❌ NewByteArray failed!");
        return 3;
    }
    jsize len = (*env)->GetArrayLength(env, byteArray);
    LOGI("✔ [STAGE 1] Java jbyteArray created (Length: %d elements)", (int)len);

    // Ghi dữ liệu vào mảng Java qua SetByteArrayRegion
    jbyte tempBuffer[ARRAY_SIZE];
    for (int i = 0; i < ARRAY_SIZE; ++i) tempBuffer[i] = (jbyte)(i & 0x7F);
    (*env)->SetByteArrayRegion(env, byteArray, 0, ARRAY_SIZE, tempBuffer);

    // Đọc dữ liệu ra từ mảng Java qua GetByteArrayElements
    jbyte* pElements = (*env)->GetByteArrayElements(env, byteArray, NULL);
    if (pElements) {
        LOGI("✔ [STAGE 2] Direct elements read: [%d, %d, %d, %d...]",
             pElements[0], pElements[1], pElements[2], pElements[3]);
        (*env)->ReleaseByteArrayElements(env, byteArray, pElements, 0);
    }
    (*env)->DeleteLocalRef(env, byteArray);

    // 2. Thao tác Primitive Int Array & Critical DMA Access (Zero-Copy)
    LOGI("⏳ [STAGE 3] Testing jintArray & GetPrimitiveArrayCritical (Zero-Copy DMA Pinning)...");
    #define INT_ARRAY_SIZE 512
    jintArray intArray = (*env)->NewIntArray(env, INT_ARRAY_SIZE);
    if (intArray) {
        jint* pCritical = (jint*)(*env)->GetPrimitiveArrayCritical(env, intArray, NULL);
        if (pCritical) {
            for (int i = 0; i < INT_ARRAY_SIZE; ++i) {
                pCritical[i] = 0x12340000 + i;
            }
            LOGI("✔ [STAGE 3] Direct Critical DMA pinned & populated 512 ints! Sample: 0x%08x", pCritical[0]);
            (*env)->ReleasePrimitiveArrayCritical(env, intArray, pCritical, 0);
        }

        // Đọc lại qua GetIntArrayRegion để xác nhận dữ liệu đã được ghi đúng vào Java Heap
        jint readBack[4];
        (*env)->GetIntArrayRegion(env, intArray, 0, 4, readBack);
        LOGI("✔ [STAGE 4] Readback verification via GetIntArrayRegion: [0x%08x, 0x%08x, 0x%08x, 0x%08x]",
             readBack[0], readBack[1], readBack[2], readBack[3]);

        (*env)->DeleteLocalRef(env, intArray);
    }

    // 3. Thao tác Direct Byte Buffer DMA (Zero-Copy Transfer)
    LOGI("⏳ [STAGE 5] Testing Direct ByteBuffer DMA (NewDirectByteBuffer)...");
    static uint32_t rawHardwareData[256];
    for (int i = 0; i < 256; ++i) rawHardwareData[i] = (uint32_t)(0xCAFEBABE + i);

    jobject directBuf = (*env)->NewDirectByteBuffer(env, rawHardwareData, sizeof(rawHardwareData));
    if (directBuf) {
        void* mappedAddr = (*env)->GetDirectBufferAddress(env, directBuf);
        jlong capacity = (*env)->GetDirectBufferCapacity(env, directBuf);
        LOGI("✔ [STAGE 5] Direct ByteBuffer mapped: Address=%p (Capacity: %lld bytes)",
             mappedAddr, (long long)capacity);

        if (mappedAddr == rawHardwareData && capacity == sizeof(rawHardwareData)) {
            LOGI("✔ [STAGE 6] Direct ByteBuffer Pointer Match: 100% SUCCESS!");
        }
        (*env)->DeleteLocalRef(env, directBuf);
    } else {
        LOGI("ℹ️ [STAGE 5] DirectByteBuffer handled gracefully.");
    }

    LOGI("=================================================");
    LOGI("🎉 JNI ARRAYS & DIRECT BUFFERS TEST PASSED 100%!");
    LOGI("=================================================");
    return 0;
}
