#ifndef JNI_STANDALONE_H
#define JNI_STANDALONE_H

typedef unsigned char uint8_t;
typedef signed char int8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef int int32_t;
typedef short int16_t;
typedef unsigned long long uint64_t;
typedef long long int64_t;
typedef unsigned long size_t;

typedef int32_t jint;
typedef int64_t jlong;
typedef int8_t  jbyte;
typedef uint8_t jboolean;
typedef uint16_t jchar;
typedef int16_t jshort;
typedef float   jfloat;
typedef double  jdouble;
typedef jint    jsize;

#define JNI_FALSE 0
#define JNI_TRUE  1
#define JNI_OK    0
#define JNI_ERR   (-1)
#define JNI_VERSION_1_6 0x00010006
#define NULL ((void*)0)

typedef void* jobject;
typedef jobject jclass;
typedef jobject jthrowable;
typedef jobject jstring;
typedef jobject jarray;
typedef jarray jbooleanArray;
typedef jarray jbyteArray;
typedef jarray jcharArray;
typedef jarray jshortArray;
typedef jarray jintArray;
typedef jarray jlongArray;
typedef jarray jfloatArray;
typedef jarray jdoubleArray;
typedef jarray jobjectArray;
typedef jobject jweak;

typedef union jvalue {
    jboolean z;
    jbyte    b;
    jchar    c;
    jshort   s;
    jint     i;
    jlong    j;
    jfloat   f;
    jdouble  d;
    jobject  l;
} jvalue;

struct _jfieldID;
typedef struct _jfieldID *jfieldID;

struct _jmethodID;
typedef struct _jmethodID *jmethodID;

typedef struct {
    char *name;
    char *signature;
    void *fnPtr;
} JNINativeMethod;

struct JNINativeInterface_;
struct JNIInvokeInterface_;

typedef const struct JNINativeInterface_ *JNIEnv;
typedef const struct JNIInvokeInterface_ *JavaVM;

typedef struct JavaVMAttachArgs {
    jint version;
    char *name;
    jobject group;
} JavaVMAttachArgs;

struct JNIInvokeInterface_ {
    void *reserved0;
    void *reserved1;
    void *reserved2;

    jint (*DestroyJavaVM)(JavaVM *vm);
    jint (*AttachCurrentThread)(JavaVM *vm, void **penv, void *args);
    jint (*DetachCurrentThread)(JavaVM *vm);
    jint (*GetEnv)(JavaVM *vm, void **penv, jint version);
    jint (*AttachCurrentThreadAsDaemon)(JavaVM *vm, void **penv, void *args);
};

struct JNINativeInterface_ {
    void *reserved0;
    void *reserved1;
    void *reserved2;
    void *reserved3;

    jint (*GetVersion)(JNIEnv *env);
    jclass (*DefineClass)(JNIEnv *env, const char *name, jobject loader, const jbyte *buf, jsize len);
    jclass (*FindClass)(JNIEnv *env, const char *name);

    jmethodID (*FromReflectedMethod)(JNIEnv *env, jobject method);
    jfieldID (*FromReflectedField)(JNIEnv *env, jobject field);
    jobject (*ToReflectedMethod)(JNIEnv *env, jclass cls, jmethodID methodID, jboolean isStatic);
    jclass (*GetSuperclass)(JNIEnv *env, jclass sub);
    jboolean (*IsAssignableFrom)(JNIEnv *env, jclass sub, jclass sup);
    jobject (*ToReflectedField)(JNIEnv *env, jclass cls, jfieldID fieldID, jboolean isStatic);

    jint (*Throw)(JNIEnv *env, jthrowable obj);
    jint (*ThrowNew)(JNIEnv *env, jclass clazz, const char *msg);
    jthrowable (*ExceptionOccurred)(JNIEnv *env);
    void (*ExceptionDescribe)(JNIEnv *env);
    void (*ExceptionClear)(JNIEnv *env);
    void (*FatalError)(JNIEnv *env, const char *msg);

    jint (*PushLocalFrame)(JNIEnv *env, jint capacity);
    jobject (*PopLocalFrame)(JNIEnv *env, jobject result);

    jobject (*NewGlobalRef)(JNIEnv *env, jobject lobj);
    void (*DeleteGlobalRef)(JNIEnv *env, jobject gref);
    void (*DeleteLocalRef)(JNIEnv *env, jobject obj);
    jboolean (*IsSameObject)(JNIEnv *env, jobject obj1, jobject obj2);
    jobject (*NewLocalRef)(JNIEnv *env, jobject ref);
    jint (*EnsureLocalCapacity)(JNIEnv *env, jint capacity);

    jobject (*AllocObject)(JNIEnv *env, jclass clazz);
    jobject (*NewObject)(JNIEnv *env, jclass clazz, jmethodID methodID, ...);
    void* newObjectV;
    jobject (*NewObjectA)(JNIEnv *env, jclass clazz, jmethodID methodID, const jvalue *args);

    jclass (*GetObjectClass)(JNIEnv *env, jobject obj);
    jboolean (*IsInstanceOf)(JNIEnv *env, jobject obj, jclass clazz);
    jmethodID (*GetMethodID)(JNIEnv *env, jclass clazz, const char *name, const char *sig);

    // CallObjectMethod (3)
    jobject (*CallObjectMethod)(JNIEnv *env, jobject obj, jmethodID methodID, ...);
    void* callObjectMethodV;
    jobject (*CallObjectMethodA)(JNIEnv *env, jobject obj, jmethodID methodID, const jvalue * args);

    // CallBooleanMethod (3)
    jboolean (*CallBooleanMethod)(JNIEnv *env, jobject obj, jmethodID methodID, ...);
    void* callBooleanMethodV;
    jboolean (*CallBooleanMethodA)(JNIEnv *env, jobject obj, jmethodID methodID, const jvalue * args);

    // CallByteMethod (3)
    jbyte (*CallByteMethod)(JNIEnv *env, jobject obj, jmethodID methodID, ...);
    void* callByteMethodV;
    jbyte (*CallByteMethodA)(JNIEnv *env, jobject obj, jmethodID methodID, const jvalue *args);

    // CallCharMethod (3)
    jchar (*CallCharMethod)(JNIEnv *env, jobject obj, jmethodID methodID, ...);
    void* callCharMethodV;
    jchar (*CallCharMethodA)(JNIEnv *env, jobject obj, jmethodID methodID, const jvalue *args);

    // CallShortMethod (3)
    jshort (*CallShortMethod)(JNIEnv *env, jobject obj, jmethodID methodID, ...);
    void* callShortMethodV;
    jshort (*CallShortMethodA)(JNIEnv *env, jobject obj, jmethodID methodID, const jvalue *args);

    // CallIntMethod (3)
    jint (*CallIntMethod)(JNIEnv *env, jobject obj, jmethodID methodID, ...);
    void* callIntMethodV;
    jint (*CallIntMethodA)(JNIEnv *env, jobject obj, jmethodID methodID, const jvalue *args);

    // CallLongMethod (3)
    jlong (*CallLongMethod)(JNIEnv *env, jobject obj, jmethodID methodID, ...);
    void* callLongMethodV;
    jlong (*CallLongMethodA)(JNIEnv *env, jobject obj, jmethodID methodID, const jvalue *args);

    // CallFloatMethod (3)
    jfloat (*CallFloatMethod)(JNIEnv *env, jobject obj, jmethodID methodID, ...);
    void* callFloatMethodV;
    jfloat (*CallFloatMethodA)(JNIEnv *env, jobject obj, jmethodID methodID, const jvalue *args);

    // CallDoubleMethod (3)
    jdouble (*CallDoubleMethod)(JNIEnv *env, jobject obj, jmethodID methodID, ...);
    void* callDoubleMethodV;
    jdouble (*CallDoubleMethodA)(JNIEnv *env, jobject obj, jmethodID methodID, const jvalue *args);

    // CallVoidMethod (3)
    void (*CallVoidMethod)(JNIEnv *env, jobject obj, jmethodID methodID, ...);
    void* callVoidMethodV;
    void (*CallVoidMethodA)(JNIEnv *env, jobject obj, jmethodID methodID, const jvalue * args);

    // CallNonvirtual Methods (30)
    void* callNonvirtObj[3];
    void* callNonvirtBool[3];
    void* callNonvirtByte[3];
    void* callNonvirtChar[3];
    void* callNonvirtShort[3];
    void* callNonvirtInt[3];
    void* callNonvirtLong[3];
    void* callNonvirtFloat[3];
    void* callNonvirtDouble[3];
    void* callNonvirtVoid[3];

    // Field Access (10 getters, 10 setters)
    jfieldID (*GetFieldID)(JNIEnv *env, jclass clazz, const char *name, const char *sig);
    jobject (*GetObjectField)(JNIEnv *env, jobject obj, jfieldID fieldID);
    jboolean (*GetBooleanField)(JNIEnv *env, jobject obj, jfieldID fieldID);
    jbyte (*GetByteField)(JNIEnv *env, jobject obj, jfieldID fieldID);
    jchar (*GetCharField)(JNIEnv *env, jobject obj, jfieldID fieldID);
    jshort (*GetShortField)(JNIEnv *env, jobject obj, jfieldID fieldID);
    jint (*GetIntField)(JNIEnv *env, jobject obj, jfieldID fieldID);
    jlong (*GetLongField)(JNIEnv *env, jobject obj, jfieldID fieldID);
    jfloat (*GetFloatField)(JNIEnv *env, jobject obj, jfieldID fieldID);
    jdouble (*GetDoubleField)(JNIEnv *env, jobject obj, jfieldID fieldID);

    void (*SetObjectField)(JNIEnv *env, jobject obj, jfieldID fieldID, jobject val);
    void (*SetBooleanField)(JNIEnv *env, jobject obj, jfieldID fieldID, jboolean val);
    void (*SetByteField)(JNIEnv *env, jobject obj, jfieldID fieldID, jbyte val);
    void (*SetCharField)(JNIEnv *env, jobject obj, jfieldID fieldID, jchar val);
    void (*SetShortField)(JNIEnv *env, jobject obj, jfieldID fieldID, jshort val);
    void (*SetIntField)(JNIEnv *env, jobject obj, jfieldID fieldID, jint val);
    void (*SetLongField)(JNIEnv *env, jobject obj, jfieldID fieldID, jlong val);
    void (*SetFloatField)(JNIEnv *env, jobject obj, jfieldID fieldID, jfloat val);
    void (*SetDoubleField)(JNIEnv *env, jobject obj, jfieldID fieldID, jdouble val);

    // Static Methods (10 types * 3 = 30)
    jmethodID (*GetStaticMethodID)(JNIEnv *env, jclass clazz, const char *name, const char *sig);
    jobject (*CallStaticObjectMethod)(JNIEnv *env, jclass clazz, jmethodID methodID, ...);
    void* callStaticObjectMethodV;
    void* callStaticObjectMethodA;
    jboolean (*CallStaticBooleanMethod)(JNIEnv *env, jclass clazz, jmethodID methodID, ...);
    void* callStaticBooleanMethodV;
    void* callStaticBooleanMethodA;
    void* callStaticByte[3];
    void* callStaticChar[3];
    void* callStaticShort[3];
    jint (*CallStaticIntMethod)(JNIEnv *env, jclass clazz, jmethodID methodID, ...);
    void* callStaticIntMethodV;
    void* callStaticIntMethodA;
    void* callStaticLong[3];
    void* callStaticFloat[3];
    void* callStaticDouble[3];
    void (*CallStaticVoidMethod)(JNIEnv *env, jclass cls, jmethodID methodID, ...);
    void* callStaticVoidMethodV;
    void* callStaticVoidMethodA;

    // Static Fields (1 ID + 9 getters + 9 setters = 19)
    jfieldID (*GetStaticFieldID)(JNIEnv *env, jclass clazz, const char *name, const char *sig);
    jobject (*GetStaticObjectField)(JNIEnv *env, jclass clazz, jfieldID fieldID);
    jboolean (*GetStaticBooleanField)(JNIEnv *env, jclass clazz, jfieldID fieldID);
    jbyte (*GetStaticByteField)(JNIEnv *env, jclass clazz, jfieldID fieldID);
    jchar (*GetStaticCharField)(JNIEnv *env, jclass clazz, jfieldID fieldID);
    jshort (*GetStaticShortField)(JNIEnv *env, jclass clazz, jfieldID fieldID);
    jint (*GetStaticIntField)(JNIEnv *env, jclass clazz, jfieldID fieldID);
    jlong (*GetStaticLongField)(JNIEnv *env, jclass clazz, jfieldID fieldID);
    jfloat (*GetStaticFloatField)(JNIEnv *env, jclass clazz, jfieldID fieldID);
    jdouble (*GetStaticDoubleField)(JNIEnv *env, jclass clazz, jfieldID fieldID);

    void* setStaticFields[9];

    // Strings (8)
    jstring (*NewString)(JNIEnv *env, const jchar *unicode, jsize len);
    jsize (*GetStringLength)(JNIEnv *env, jstring str);
    const jchar *(*GetStringChars)(JNIEnv *env, jstring str, jboolean *isCopy);
    void (*ReleaseStringChars)(JNIEnv *env, jstring str, const jchar *chars);
    jstring (*NewStringUTF)(JNIEnv *env, const char *utf);
    jsize (*GetStringUTFLength)(JNIEnv *env, jstring str);
    const char* (*GetStringUTFChars)(JNIEnv *env, jstring str, jboolean *isCopy);
    void (*ReleaseStringUTFChars)(JNIEnv *env, jstring str, const char* chars);

    // Arrays (1 len + 3 obj array + 8 new + 8 get + 8 release + 8 getRegion + 8 setRegion = 44)
    jsize (*GetArrayLength)(JNIEnv *env, jarray array);
    jobjectArray (*NewObjectArray)(JNIEnv *env, jsize len, jclass clazz, jobject init);
    jobject (*GetObjectArrayElement)(JNIEnv *env, jobjectArray array, jsize index);
    void (*SetObjectArrayElement)(JNIEnv *env, jobjectArray array, jsize index, jobject val);

    jbooleanArray (*NewBooleanArray)(JNIEnv *env, jsize len);
    jbyteArray (*NewByteArray)(JNIEnv *env, jsize len);
    jcharArray (*NewCharArray)(JNIEnv *env, jsize len);
    jshortArray (*NewShortArray)(JNIEnv *env, jsize len);
    jintArray (*NewIntArray)(JNIEnv *env, jsize len);
    jlongArray (*NewLongArray)(JNIEnv *env, jsize len);
    jfloatArray (*NewFloatArray)(JNIEnv *env, jsize len);
    jdoubleArray (*NewDoubleArray)(JNIEnv *env, jsize len);

    jboolean * (*GetBooleanArrayElements)(JNIEnv *env, jbooleanArray array, jboolean *isCopy);
    jbyte * (*GetByteArrayElements)(JNIEnv *env, jbyteArray array, jboolean *isCopy);
    jchar * (*GetCharArrayElements)(JNIEnv *env, jcharArray array, jboolean *isCopy);
    jshort * (*GetShortArrayElements)(JNIEnv *env, jshortArray array, jboolean *isCopy);
    jint * (*GetIntArrayElements)(JNIEnv *env, jintArray array, jboolean *isCopy);
    jlong * (*GetLongArrayElements)(JNIEnv *env, jlongArray array, jboolean *isCopy);
    jfloat * (*GetFloatArrayElements)(JNIEnv *env, jfloatArray array, jboolean *isCopy);
    jdouble * (*GetDoubleArrayElements)(JNIEnv *env, jdoubleArray array, jboolean *isCopy);

    void (*ReleaseBooleanArrayElements)(JNIEnv *env, jbooleanArray array, jboolean *elems, jint mode);
    void (*ReleaseByteArrayElements)(JNIEnv *env, jbyteArray array, jbyte *elems, jint mode);
    void (*ReleaseCharArrayElements)(JNIEnv *env, jcharArray array, jchar *elems, jint mode);
    void (*ReleaseShortArrayElements)(JNIEnv *env, jshortArray array, jshort *elems, jint mode);
    void (*ReleaseIntArrayElements)(JNIEnv *env, jintArray array, jint *elems, jint mode);
    void (*ReleaseLongArrayElements)(JNIEnv *env, jlongArray array, jlong *elems, jint mode);
    void (*ReleaseFloatArrayElements)(JNIEnv *env, jfloatArray array, jfloat *elems, jint mode);
    void (*ReleaseDoubleArrayElements)(JNIEnv *env, jdoubleArray array, jdouble *elems, jint mode);

    void (*GetBooleanArrayRegion)(JNIEnv *env, jbooleanArray array, jsize start, jsize l, jboolean *buf);
    void (*GetByteArrayRegion)(JNIEnv *env, jbyteArray array, jsize start, jsize len, jbyte *buf);
    void (*GetCharArrayRegion)(JNIEnv *env, jcharArray array, jsize start, jsize len, jchar *buf);
    void (*GetShortArrayRegion)(JNIEnv *env, jshortArray array, jsize start, jsize len, jshort *buf);
    void (*GetIntArrayRegion)(JNIEnv *env, jintArray array, jsize start, jsize len, jint *buf);
    void (*GetLongArrayRegion)(JNIEnv *env, jlongArray array, jsize start, jsize len, jlong *buf);
    void (*GetFloatArrayRegion)(JNIEnv *env, jfloatArray array, jsize start, jsize len, jfloat *buf);
    void (*GetDoubleArrayRegion)(JNIEnv *env, jdoubleArray array, jsize start, jsize len, jdouble *buf);

    void (*SetBooleanArrayRegion)(JNIEnv *env, jbooleanArray array, jsize start, jsize l, const jboolean *buf);
    void (*SetByteArrayRegion)(JNIEnv *env, jbyteArray array, jsize start, jsize len, const jbyte *buf);
    void (*SetCharArrayRegion)(JNIEnv *env, jcharArray array, jsize start, jsize len, const jchar *buf);
    void (*SetShortArrayRegion)(JNIEnv *env, jshortArray array, jsize start, jsize len, const jshort *buf);
    void (*SetIntArrayRegion)(JNIEnv *env, jintArray array, jsize start, jsize len, const jint *buf);
    void (*SetLongArrayRegion)(JNIEnv *env, jlongArray array, jsize start, jsize len, const jlong *buf);
    void (*SetFloatArrayRegion)(JNIEnv *env, jfloatArray array, jsize start, jsize len, const jfloat *buf);
    void (*SetDoubleArrayRegion)(JNIEnv *env, jdoubleArray array, jsize start, jsize len, const jdouble *buf);

    // Dynamic Linking & VM (5)
    jint (*RegisterNatives)(JNIEnv *env, jclass clazz, const JNINativeMethod *methods, jint nMethods);
    jint (*UnregisterNatives)(JNIEnv *env, jclass clazz);
    jint (*MonitorEnter)(JNIEnv *env, jobject obj);
    jint (*MonitorExit)(JNIEnv *env, jobject obj);
    jint (*GetJavaVM)(JNIEnv *env, JavaVM **vm);

    // Advanced & JNI 1.4 / 1.6 (12)
    void (*GetStringRegion)(JNIEnv *env, jstring str, jsize start, jsize len, jchar *buf);
    void (*GetStringUTFRegion)(JNIEnv *env, jstring str, jsize start, jsize len, char *buf);
    void * (*GetPrimitiveArrayCritical)(JNIEnv *env, jarray array, jboolean *isCopy);
    void (*ReleasePrimitiveArrayCritical)(JNIEnv *env, jarray array, void *carray, jint mode);
    const jchar * (*GetStringCritical)(JNIEnv *env, jstring string, jboolean *isCopy);
    void (*ReleaseStringCritical)(JNIEnv *env, jstring string, const jchar *cstring);
    jweak (*NewWeakGlobalRef)(JNIEnv *env, jobject obj);
    void (*DeleteWeakGlobalRef)(JNIEnv *env, jweak ref);
    jboolean (*ExceptionCheck)(JNIEnv *env);
    jobject (*NewDirectByteBuffer)(JNIEnv* env, void* address, jlong capacity);
    void* (*GetDirectBufferAddress)(JNIEnv* env, jobject buf);
    jlong (*GetDirectBufferCapacity)(JNIEnv* env, jobject buf);
};

#endif // JNI_STANDALONE_H
