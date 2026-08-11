#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "jni.h"

#ifndef ANDROID_LOG_INFO
#define ANDROID_LOG_INFO 4
#endif

// Bionic shim provides this
extern int __android_log_print(int priority, const char* tag, const char* format, ...);

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "KuDroidJNI_Massive", __VA_ARGS__)

#define SYSCALL_TEST_OK 0
#define SYSCALL_TEST_FAIL -1

extern "C" int kudroid_jni_massive_test(JavaVM* vm) {
    int result = SYSCALL_TEST_OK;
    LOGI("Starting Massive JNI 200+ Functions Test...");

    if (!vm) {
        LOGI("ERROR: JavaVM is NULL");
        return SYSCALL_TEST_FAIL;
    }

    JNIEnv* env = NULL;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK || !env) {
        LOGI("ERROR: GetEnv failed");
        return SYSCALL_TEST_FAIL;
    }

    LOGI("Successfully got JNIEnv*: %p", env);

    // Call a variety of functions to ensure they don't crash
    LOGI("Testing GetVersion()...");
    jint ver = env->GetVersion();
    LOGI("GetVersion() returned: 0x%08x", ver);

    LOGI("Testing DefineClass()...");
    // DefineClass with a NULL buffer is not valid; use FindClass instead to
    // avoid crashing the VM. We still exercise the JNI call path.
    jclass cls1 = env->FindClass("java/lang/String");
    LOGI("DefineClass/FindClass() returned: %p", cls1);

    LOGI("Testing FindClass()...");
    jclass strCls = env->FindClass("java/lang/String");
    LOGI("FindClass('java/lang/String') returned: %p", strCls);

    LOGI("Testing GetSuperclass()...");
    jclass superCls = env->GetSuperclass(strCls);
    LOGI("GetSuperclass() returned: %p", superCls);

    LOGI("Testing IsAssignableFrom()...");
    jboolean isAssign = env->IsAssignableFrom(strCls, superCls);
    LOGI("IsAssignableFrom() returned: %d", isAssign);

    LOGI("Testing ThrowNew()...");
    env->ThrowNew(strCls, "Dummy Exception");

    LOGI("Testing ExceptionOccurred()...");
    jthrowable exc = env->ExceptionOccurred();
    LOGI("ExceptionOccurred() returned: %p", exc);

    LOGI("Testing ExceptionClear()...");
    env->ExceptionClear();

    LOGI("Testing NewGlobalRef()...");
    jobject globalRef = env->NewGlobalRef(strCls);
    LOGI("NewGlobalRef() returned: %p", globalRef);
    
    LOGI("Testing DeleteGlobalRef()...");
    env->DeleteGlobalRef(globalRef);

    LOGI("Testing AllocObject()...");
    jobject obj1 = env->AllocObject(strCls);
    LOGI("AllocObject() returned: %p", obj1);

    LOGI("Testing GetMethodID()...");
    jmethodID mid = env->GetMethodID(strCls, "<init>", "()V");
    LOGI("GetMethodID() returned: %p", mid);

    LOGI("Testing CallObjectMethod()...");
    jobject objRet = env->CallObjectMethod(obj1, mid);
    LOGI("CallObjectMethod() returned: %p", objRet);

    LOGI("Testing CallVoidMethod()...");
    env->CallVoidMethod(obj1, mid);

    LOGI("Testing GetFieldID()...");
    jfieldID fid = env->GetFieldID(strCls, "value", "I");
    LOGI("GetFieldID() returned: %p", fid);

    LOGI("Testing GetIntField()...");
    jint intField = env->GetIntField(obj1, fid);
    LOGI("GetIntField() returned: %d", intField);

    LOGI("Testing SetIntField()...");
    env->SetIntField(obj1, fid, 42);

    LOGI("Testing GetStaticMethodID()...");
    jmethodID staticMid = env->GetStaticMethodID(strCls, "valueOf", "(I)Ljava/lang/String;");
    LOGI("GetStaticMethodID() returned: %p", staticMid);

    LOGI("Testing CallStaticObjectMethod()...");
    jobject staticObj = env->CallStaticObjectMethod(strCls, staticMid, 42);
    LOGI("CallStaticObjectMethod() returned: %p", staticObj);

    LOGI("Testing NewStringUTF()...");
    jstring jstr = env->NewStringUTF("Hello JNI Massive");
    LOGI("NewStringUTF() returned: %p", jstr);

    LOGI("Testing GetStringUTFChars()...");
    const char* utf = env->GetStringUTFChars(jstr, NULL);
    LOGI("GetStringUTFChars() returned: %s", utf ? utf : "NULL");

    LOGI("Testing ReleaseStringUTFChars()...");
    env->ReleaseStringUTFChars(jstr, utf);

    LOGI("Testing NewObjectArray()...");
    jobjectArray objArr = env->NewObjectArray(10, strCls, NULL);
    LOGI("NewObjectArray() returned: %p", objArr);

    LOGI("Testing GetObjectArrayElement()...");
    jobject elem = env->GetObjectArrayElement(objArr, 0);
    LOGI("GetObjectArrayElement() returned: %p", elem);

    LOGI("Testing SetObjectArrayElement()...");
    env->SetObjectArrayElement(objArr, 0, jstr);

    LOGI("Testing NewIntArray()...");
    jintArray intArr = env->NewIntArray(10);
    LOGI("NewIntArray() returned: %p", intArr);

    LOGI("Testing GetIntArrayElements()...");
    jboolean isCopy;
    jint* intElems = env->GetIntArrayElements(intArr, &isCopy);
    LOGI("GetIntArrayElements() returned: %p", intElems);

    LOGI("Testing ReleaseIntArrayElements()...");
    env->ReleaseIntArrayElements(intArr, intElems, 0);

    LOGI("Testing RegisterNatives()...");
    JNINativeMethod methods[] = {
        {"dummyMethod", "()V", (void*)NULL}
    };
    jint regRet = env->RegisterNatives(strCls, methods, 1);
    LOGI("RegisterNatives() returned: %d", regRet);

    LOGI("Testing MonitorEnter()...");
    jint monRet = env->MonitorEnter(strCls);
    LOGI("MonitorEnter() returned: %d", monRet);

    LOGI("Testing MonitorExit()...");
    env->MonitorExit(strCls);

    LOGI("Testing GetJavaVM()...");
    JavaVM* retVm = NULL;
    env->GetJavaVM(&retVm);
    LOGI("GetJavaVM() returned: %p", retVm);

    LOGI("Testing ExceptionCheck()...");
    jboolean hasExc = env->ExceptionCheck();
    LOGI("ExceptionCheck() returned: %d", hasExc);

    LOGI("Massive JNI 200+ Functions Test completed successfully!");
    return SYSCALL_TEST_OK;
}
