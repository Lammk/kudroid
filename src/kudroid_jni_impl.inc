// AUTO-GENERATED JNI BRIDGE IMPLEMENTATION
// DO NOT EDIT DIRECTLY

static jclass JNICALL jni_DefineClass(JNIEnv *env, const char *name, jobject loader, const jbyte *buf, jsize len) {
    log_jni("[AUTO] DefineClass(" "env=%p, name=%p, loader=%p, buf=%d, len=%d "")", (void*)env, (void*)name, (void*)loader, (int)buf, (int)len);
    return nullptr;
}

static jmethodID JNICALL jni_FromReflectedMethod(JNIEnv *env, jobject method) {
    log_jni("[AUTO] FromReflectedMethod(" "env=%p, method=%p "")", (void*)env, (void*)method);
    return nullptr;
}

static jfieldID JNICALL jni_FromReflectedField(JNIEnv *env, jobject field) {
    log_jni("[AUTO] FromReflectedField(" "env=%p, field=%p "")", (void*)env, (void*)field);
    return nullptr;
}

static jobject JNICALL jni_ToReflectedMethod(JNIEnv *env, jclass cls, jmethodID methodID, jboolean isStatic) {
    log_jni("[AUTO] ToReflectedMethod(" "env=%p, cls=%p, methodID=%p, isStatic=%d "")", (void*)env, (void*)cls, (void*)methodID, (int)isStatic);
    return nullptr;
}

static jclass JNICALL jni_GetSuperclass(JNIEnv *env, jclass sub) {
    log_jni("[AUTO] GetSuperclass(" "env=%p, sub=%p "")", (void*)env, (void*)sub);
    return nullptr;
}

static jboolean JNICALL jni_IsAssignableFrom(JNIEnv *env, jclass sub, jclass sup) {
    log_jni("[AUTO] IsAssignableFrom(" "env=%p, sub=%p, sup=%p "")", (void*)env, (void*)sub, (void*)sup);
    return JNI_FALSE;
}

static jobject JNICALL jni_ToReflectedField(JNIEnv *env, jclass cls, jfieldID fieldID, jboolean isStatic) {
    log_jni("[AUTO] ToReflectedField(" "env=%p, cls=%p, fieldID=%p, isStatic=%d "")", (void*)env, (void*)cls, (void*)fieldID, (int)isStatic);
    return nullptr;
}

static jint JNICALL jni_Throw(JNIEnv *env, jthrowable obj) {
    log_jni("[AUTO] Throw(" "env=%p, obj=%p "")", (void*)env, (void*)obj);
    return 0;
}

static jint JNICALL jni_ThrowNew(JNIEnv *env, jclass clazz, const char *msg) {
    log_jni("[AUTO] ThrowNew(" "env=%p, clazz=%p, msg=%p "")", (void*)env, (void*)clazz, (void*)msg);
    return 0;
}

static jthrowable JNICALL jni_ExceptionOccurred(JNIEnv *env) {
    log_jni("[AUTO] ExceptionOccurred(" "env=%p "")", (void*)env);
    return nullptr;
}

static void JNICALL jni_ExceptionDescribe(JNIEnv *env) {
    log_jni("[AUTO] ExceptionDescribe(" "env=%p "")", (void*)env);
    return;
}

static void JNICALL jni_ExceptionClear(JNIEnv *env) {
    log_jni("[AUTO] ExceptionClear(" "env=%p "")", (void*)env);
    return;
}

static void JNICALL jni_FatalError(JNIEnv *env, const char *msg) {
    log_jni("[AUTO] FatalError(" "env=%p, msg=%p "")", (void*)env, (void*)msg);
    return;
}

static jint JNICALL jni_PushLocalFrame(JNIEnv *env, jint capacity) {
    log_jni("[AUTO] PushLocalFrame(" "env=%p, capacity=%d "")", (void*)env, (int)capacity);
    return 0;
}

static jobject JNICALL jni_PopLocalFrame(JNIEnv *env, jobject result) {
    log_jni("[AUTO] PopLocalFrame(" "env=%p, result=%p "")", (void*)env, (void*)result);
    return nullptr;
}

static jobject JNICALL jni_NewGlobalRef(JNIEnv *env, jobject lobj) {
    log_jni("[AUTO] NewGlobalRef(" "env=%p, lobj=%p "")", (void*)env, (void*)lobj);
    return nullptr;
}

static void JNICALL jni_DeleteGlobalRef(JNIEnv *env, jobject gref) {
    log_jni("[AUTO] DeleteGlobalRef(" "env=%p, gref=%p "")", (void*)env, (void*)gref);
    return;
}

static void JNICALL jni_DeleteLocalRef(JNIEnv *env, jobject obj) {
    log_jni("[AUTO] DeleteLocalRef(" "env=%p, obj=%p "")", (void*)env, (void*)obj);
    return;
}

static jboolean JNICALL jni_IsSameObject(JNIEnv *env, jobject obj1, jobject obj2) {
    log_jni("[AUTO] IsSameObject(" "env=%p, obj1=%p, obj2=%p "")", (void*)env, (void*)obj1, (void*)obj2);
    return JNI_FALSE;
}

static jobject JNICALL jni_NewLocalRef(JNIEnv *env, jobject ref) {
    log_jni("[AUTO] NewLocalRef(" "env=%p, ref=%p "")", (void*)env, (void*)ref);
    return nullptr;
}

static jint JNICALL jni_EnsureLocalCapacity(JNIEnv *env, jint capacity) {
    log_jni("[AUTO] EnsureLocalCapacity(" "env=%p, capacity=%d "")", (void*)env, (int)capacity);
    return 0;
}

static jobject JNICALL jni_AllocObject(JNIEnv *env, jclass clazz) {
    log_jni("[AUTO] AllocObject(" "env=%p, clazz=%p "")", (void*)env, (void*)clazz);
    return nullptr;
}

static jobject JNICALL jni_NewObject(JNIEnv *env, jclass clazz, jmethodID methodID, ...) {
    log_jni("[AUTO] NewObject(" "env=%p, clazz=%p, methodID=%p "")", (void*)env, (void*)clazz, (void*)methodID);
    return nullptr;
}

static jobject JNICALL jni_NewObjectV(JNIEnv *env, jclass clazz, jmethodID methodID, va_list args) {
    log_jni("[AUTO] NewObjectV(" "env=%p, clazz=%p, methodID=%p, args=%p "")", (void*)env, (void*)clazz, (void*)methodID, (void*)args);
    return nullptr;
}

static jobject JNICALL jni_NewObjectA(JNIEnv *env, jclass clazz, jmethodID methodID, const jvalue *args) {
    log_jni("[AUTO] NewObjectA(" "env=%p, clazz=%p, methodID=%p, args=%p "")", (void*)env, (void*)clazz, (void*)methodID, (void*)args);
    return nullptr;
}

static jclass JNICALL jni_GetObjectClass(JNIEnv *env, jobject obj) {
    log_jni("[AUTO] GetObjectClass(" "env=%p, obj=%p "")", (void*)env, (void*)obj);
    return nullptr;
}

static jboolean JNICALL jni_IsInstanceOf(JNIEnv *env, jobject obj, jclass clazz) {
    log_jni("[AUTO] IsInstanceOf(" "env=%p, obj=%p, clazz=%p "")", (void*)env, (void*)obj, (void*)clazz);
    return JNI_FALSE;
}

static jobject JNICALL jni_CallObjectMethod(JNIEnv *env, jobject obj, jmethodID methodID, ...) {
    log_jni("[AUTO] CallObjectMethod(" "env=%p, obj=%p, methodID=%p "")", (void*)env, (void*)obj, (void*)methodID);
    return nullptr;
}

static jobject JNICALL jni_CallObjectMethodV(JNIEnv *env, jobject obj, jmethodID methodID, va_list args) {
    log_jni("[AUTO] CallObjectMethodV(" "env=%p, obj=%p, methodID=%p, args=%p "")", (void*)env, (void*)obj, (void*)methodID, (void*)args);
    return nullptr;
}

static jobject JNICALL jni_CallObjectMethodA(JNIEnv *env, jobject obj, jmethodID methodID, const jvalue * args) {
    log_jni("[AUTO] CallObjectMethodA(" "env=%p, obj=%p, methodID=%p, args=%p "")", (void*)env, (void*)obj, (void*)methodID, (void*)args);
    return nullptr;
}

static jboolean JNICALL jni_CallBooleanMethod(JNIEnv *env, jobject obj, jmethodID methodID, ...) {
    log_jni("[AUTO] CallBooleanMethod(" "env=%p, obj=%p, methodID=%p "")", (void*)env, (void*)obj, (void*)methodID);
    return JNI_FALSE;
}

static jboolean JNICALL jni_CallBooleanMethodV(JNIEnv *env, jobject obj, jmethodID methodID, va_list args) {
    log_jni("[AUTO] CallBooleanMethodV(" "env=%p, obj=%p, methodID=%p, args=%p "")", (void*)env, (void*)obj, (void*)methodID, (void*)args);
    return JNI_FALSE;
}

static jboolean JNICALL jni_CallBooleanMethodA(JNIEnv *env, jobject obj, jmethodID methodID, const jvalue * args) {
    log_jni("[AUTO] CallBooleanMethodA(" "env=%p, obj=%p, methodID=%p, args=%p "")", (void*)env, (void*)obj, (void*)methodID, (void*)args);
    return JNI_FALSE;
}

static jbyte JNICALL jni_CallByteMethod(JNIEnv *env, jobject obj, jmethodID methodID, ...) {
    log_jni("[AUTO] CallByteMethod(" "env=%p, obj=%p, methodID=%p "")", (void*)env, (void*)obj, (void*)methodID);
    return 0;
}

static jbyte JNICALL jni_CallByteMethodV(JNIEnv *env, jobject obj, jmethodID methodID, va_list args) {
    log_jni("[AUTO] CallByteMethodV(" "env=%p, obj=%p, methodID=%p, args=%p "")", (void*)env, (void*)obj, (void*)methodID, (void*)args);
    return 0;
}

static jbyte JNICALL jni_CallByteMethodA(JNIEnv *env, jobject obj, jmethodID methodID, const jvalue *args) {
    log_jni("[AUTO] CallByteMethodA(" "env=%p, obj=%p, methodID=%p, args=%p "")", (void*)env, (void*)obj, (void*)methodID, (void*)args);
    return 0;
}

static jchar JNICALL jni_CallCharMethod(JNIEnv *env, jobject obj, jmethodID methodID, ...) {
    log_jni("[AUTO] CallCharMethod(" "env=%p, obj=%p, methodID=%p "")", (void*)env, (void*)obj, (void*)methodID);
    return 0;
}

static jchar JNICALL jni_CallCharMethodV(JNIEnv *env, jobject obj, jmethodID methodID, va_list args) {
    log_jni("[AUTO] CallCharMethodV(" "env=%p, obj=%p, methodID=%p, args=%p "")", (void*)env, (void*)obj, (void*)methodID, (void*)args);
    return 0;
}

static jchar JNICALL jni_CallCharMethodA(JNIEnv *env, jobject obj, jmethodID methodID, const jvalue *args) {
    log_jni("[AUTO] CallCharMethodA(" "env=%p, obj=%p, methodID=%p, args=%p "")", (void*)env, (void*)obj, (void*)methodID, (void*)args);
    return 0;
}

static jshort JNICALL jni_CallShortMethod(JNIEnv *env, jobject obj, jmethodID methodID, ...) {
    log_jni("[AUTO] CallShortMethod(" "env=%p, obj=%p, methodID=%p "")", (void*)env, (void*)obj, (void*)methodID);
    return 0;
}

static jshort JNICALL jni_CallShortMethodV(JNIEnv *env, jobject obj, jmethodID methodID, va_list args) {
    log_jni("[AUTO] CallShortMethodV(" "env=%p, obj=%p, methodID=%p, args=%p "")", (void*)env, (void*)obj, (void*)methodID, (void*)args);
    return 0;
}

static jshort JNICALL jni_CallShortMethodA(JNIEnv *env, jobject obj, jmethodID methodID, const jvalue *args) {
    log_jni("[AUTO] CallShortMethodA(" "env=%p, obj=%p, methodID=%p, args=%p "")", (void*)env, (void*)obj, (void*)methodID, (void*)args);
    return 0;
}

static jint JNICALL jni_CallIntMethod(JNIEnv *env, jobject obj, jmethodID methodID, ...) {
    log_jni("[AUTO] CallIntMethod(" "env=%p, obj=%p, methodID=%p "")", (void*)env, (void*)obj, (void*)methodID);
    return 0;
}

static jint JNICALL jni_CallIntMethodV(JNIEnv *env, jobject obj, jmethodID methodID, va_list args) {
    log_jni("[AUTO] CallIntMethodV(" "env=%p, obj=%p, methodID=%p, args=%p "")", (void*)env, (void*)obj, (void*)methodID, (void*)args);
    return 0;
}

static jint JNICALL jni_CallIntMethodA(JNIEnv *env, jobject obj, jmethodID methodID, const jvalue *args) {
    log_jni("[AUTO] CallIntMethodA(" "env=%p, obj=%p, methodID=%p, args=%p "")", (void*)env, (void*)obj, (void*)methodID, (void*)args);
    return 0;
}

static jlong JNICALL jni_CallLongMethod(JNIEnv *env, jobject obj, jmethodID methodID, ...) {
    log_jni("[AUTO] CallLongMethod(" "env=%p, obj=%p, methodID=%p "")", (void*)env, (void*)obj, (void*)methodID);
    return 0;
}

static jlong JNICALL jni_CallLongMethodV(JNIEnv *env, jobject obj, jmethodID methodID, va_list args) {
    log_jni("[AUTO] CallLongMethodV(" "env=%p, obj=%p, methodID=%p, args=%p "")", (void*)env, (void*)obj, (void*)methodID, (void*)args);
    return 0;
}

static jlong JNICALL jni_CallLongMethodA(JNIEnv *env, jobject obj, jmethodID methodID, const jvalue *args) {
    log_jni("[AUTO] CallLongMethodA(" "env=%p, obj=%p, methodID=%p, args=%p "")", (void*)env, (void*)obj, (void*)methodID, (void*)args);
    return 0;
}

static jfloat JNICALL jni_CallFloatMethod(JNIEnv *env, jobject obj, jmethodID methodID, ...) {
    log_jni("[AUTO] CallFloatMethod(" "env=%p, obj=%p, methodID=%p "")", (void*)env, (void*)obj, (void*)methodID);
    return 0;
}

static jfloat JNICALL jni_CallFloatMethodV(JNIEnv *env, jobject obj, jmethodID methodID, va_list args) {
    log_jni("[AUTO] CallFloatMethodV(" "env=%p, obj=%p, methodID=%p, args=%p "")", (void*)env, (void*)obj, (void*)methodID, (void*)args);
    return 0;
}

static jfloat JNICALL jni_CallFloatMethodA(JNIEnv *env, jobject obj, jmethodID methodID, const jvalue *args) {
    log_jni("[AUTO] CallFloatMethodA(" "env=%p, obj=%p, methodID=%p, args=%p "")", (void*)env, (void*)obj, (void*)methodID, (void*)args);
    return 0;
}

static jdouble JNICALL jni_CallDoubleMethod(JNIEnv *env, jobject obj, jmethodID methodID, ...) {
    log_jni("[AUTO] CallDoubleMethod(" "env=%p, obj=%p, methodID=%p "")", (void*)env, (void*)obj, (void*)methodID);
    return 0;
}

static jdouble JNICALL jni_CallDoubleMethodV(JNIEnv *env, jobject obj, jmethodID methodID, va_list args) {
    log_jni("[AUTO] CallDoubleMethodV(" "env=%p, obj=%p, methodID=%p, args=%p "")", (void*)env, (void*)obj, (void*)methodID, (void*)args);
    return 0;
}

static jdouble JNICALL jni_CallDoubleMethodA(JNIEnv *env, jobject obj, jmethodID methodID, const jvalue *args) {
    log_jni("[AUTO] CallDoubleMethodA(" "env=%p, obj=%p, methodID=%p, args=%p "")", (void*)env, (void*)obj, (void*)methodID, (void*)args);
    return 0;
}

static void JNICALL jni_CallVoidMethod(JNIEnv *env, jobject obj, jmethodID methodID, ...) {
    log_jni("[AUTO] CallVoidMethod(" "env=%p, obj=%p, methodID=%p "")", (void*)env, (void*)obj, (void*)methodID);
    return;
}

static void JNICALL jni_CallVoidMethodV(JNIEnv *env, jobject obj, jmethodID methodID, va_list args) {
    log_jni("[AUTO] CallVoidMethodV(" "env=%p, obj=%p, methodID=%p, args=%p "")", (void*)env, (void*)obj, (void*)methodID, (void*)args);
    return;
}

static void JNICALL jni_CallVoidMethodA(JNIEnv *env, jobject obj, jmethodID methodID, const jvalue * args) {
    log_jni("[AUTO] CallVoidMethodA(" "env=%p, obj=%p, methodID=%p, args=%p "")", (void*)env, (void*)obj, (void*)methodID, (void*)args);
    return;
}

static jobject JNICALL jni_CallNonvirtualObjectMethod(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, ...) {
    log_jni("[AUTO] CallNonvirtualObjectMethod(" "env=%p, obj=%p, clazz=%p, methodID=%p "")", (void*)env, (void*)obj, (void*)clazz, (void*)methodID);
    return nullptr;
}

static jobject JNICALL jni_CallNonvirtualObjectMethodV(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, va_list args) {
    log_jni("[AUTO] CallNonvirtualObjectMethodV(" "env=%p, obj=%p, clazz=%p, methodID=%p, args=%p "")", (void*)env, (void*)obj, (void*)clazz, (void*)methodID, (void*)args);
    return nullptr;
}

static jobject JNICALL jni_CallNonvirtualObjectMethodA(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, const jvalue * args) {
    log_jni("[AUTO] CallNonvirtualObjectMethodA(" "env=%p, obj=%p, clazz=%p, methodID=%p, args=%p "")", (void*)env, (void*)obj, (void*)clazz, (void*)methodID, (void*)args);
    return nullptr;
}

static jboolean JNICALL jni_CallNonvirtualBooleanMethod(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, ...) {
    log_jni("[AUTO] CallNonvirtualBooleanMethod(" "env=%p, obj=%p, clazz=%p, methodID=%p "")", (void*)env, (void*)obj, (void*)clazz, (void*)methodID);
    return JNI_FALSE;
}

static jboolean JNICALL jni_CallNonvirtualBooleanMethodV(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, va_list args) {
    log_jni("[AUTO] CallNonvirtualBooleanMethodV(" "env=%p, obj=%p, clazz=%p, methodID=%p, args=%p "")", (void*)env, (void*)obj, (void*)clazz, (void*)methodID, (void*)args);
    return JNI_FALSE;
}

static jboolean JNICALL jni_CallNonvirtualBooleanMethodA(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, const jvalue * args) {
    log_jni("[AUTO] CallNonvirtualBooleanMethodA(" "env=%p, obj=%p, clazz=%p, methodID=%p, args=%p "")", (void*)env, (void*)obj, (void*)clazz, (void*)methodID, (void*)args);
    return JNI_FALSE;
}

static jbyte JNICALL jni_CallNonvirtualByteMethod(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, ...) {
    log_jni("[AUTO] CallNonvirtualByteMethod(" "env=%p, obj=%p, clazz=%p, methodID=%p "")", (void*)env, (void*)obj, (void*)clazz, (void*)methodID);
    return 0;
}

static jbyte JNICALL jni_CallNonvirtualByteMethodV(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, va_list args) {
    log_jni("[AUTO] CallNonvirtualByteMethodV(" "env=%p, obj=%p, clazz=%p, methodID=%p, args=%p "")", (void*)env, (void*)obj, (void*)clazz, (void*)methodID, (void*)args);
    return 0;
}

static jbyte JNICALL jni_CallNonvirtualByteMethodA(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, const jvalue *args) {
    log_jni("[AUTO] CallNonvirtualByteMethodA(" "env=%p, obj=%p, clazz=%p, methodID=%p, args=%p "")", (void*)env, (void*)obj, (void*)clazz, (void*)methodID, (void*)args);
    return 0;
}

static jchar JNICALL jni_CallNonvirtualCharMethod(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, ...) {
    log_jni("[AUTO] CallNonvirtualCharMethod(" "env=%p, obj=%p, clazz=%p, methodID=%p "")", (void*)env, (void*)obj, (void*)clazz, (void*)methodID);
    return 0;
}

static jchar JNICALL jni_CallNonvirtualCharMethodV(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, va_list args) {
    log_jni("[AUTO] CallNonvirtualCharMethodV(" "env=%p, obj=%p, clazz=%p, methodID=%p, args=%p "")", (void*)env, (void*)obj, (void*)clazz, (void*)methodID, (void*)args);
    return 0;
}

static jchar JNICALL jni_CallNonvirtualCharMethodA(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, const jvalue *args) {
    log_jni("[AUTO] CallNonvirtualCharMethodA(" "env=%p, obj=%p, clazz=%p, methodID=%p, args=%p "")", (void*)env, (void*)obj, (void*)clazz, (void*)methodID, (void*)args);
    return 0;
}

static jshort JNICALL jni_CallNonvirtualShortMethod(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, ...) {
    log_jni("[AUTO] CallNonvirtualShortMethod(" "env=%p, obj=%p, clazz=%p, methodID=%p "")", (void*)env, (void*)obj, (void*)clazz, (void*)methodID);
    return 0;
}

static jshort JNICALL jni_CallNonvirtualShortMethodV(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, va_list args) {
    log_jni("[AUTO] CallNonvirtualShortMethodV(" "env=%p, obj=%p, clazz=%p, methodID=%p, args=%p "")", (void*)env, (void*)obj, (void*)clazz, (void*)methodID, (void*)args);
    return 0;
}

static jshort JNICALL jni_CallNonvirtualShortMethodA(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, const jvalue *args) {
    log_jni("[AUTO] CallNonvirtualShortMethodA(" "env=%p, obj=%p, clazz=%p, methodID=%p, args=%p "")", (void*)env, (void*)obj, (void*)clazz, (void*)methodID, (void*)args);
    return 0;
}

static jint JNICALL jni_CallNonvirtualIntMethod(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, ...) {
    log_jni("[AUTO] CallNonvirtualIntMethod(" "env=%p, obj=%p, clazz=%p, methodID=%p "")", (void*)env, (void*)obj, (void*)clazz, (void*)methodID);
    return 0;
}

static jint JNICALL jni_CallNonvirtualIntMethodV(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, va_list args) {
    log_jni("[AUTO] CallNonvirtualIntMethodV(" "env=%p, obj=%p, clazz=%p, methodID=%p, args=%p "")", (void*)env, (void*)obj, (void*)clazz, (void*)methodID, (void*)args);
    return 0;
}

static jint JNICALL jni_CallNonvirtualIntMethodA(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, const jvalue *args) {
    log_jni("[AUTO] CallNonvirtualIntMethodA(" "env=%p, obj=%p, clazz=%p, methodID=%p, args=%p "")", (void*)env, (void*)obj, (void*)clazz, (void*)methodID, (void*)args);
    return 0;
}

static jlong JNICALL jni_CallNonvirtualLongMethod(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, ...) {
    log_jni("[AUTO] CallNonvirtualLongMethod(" "env=%p, obj=%p, clazz=%p, methodID=%p "")", (void*)env, (void*)obj, (void*)clazz, (void*)methodID);
    return 0;
}

static jlong JNICALL jni_CallNonvirtualLongMethodV(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, va_list args) {
    log_jni("[AUTO] CallNonvirtualLongMethodV(" "env=%p, obj=%p, clazz=%p, methodID=%p, args=%p "")", (void*)env, (void*)obj, (void*)clazz, (void*)methodID, (void*)args);
    return 0;
}

static jlong JNICALL jni_CallNonvirtualLongMethodA(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, const jvalue *args) {
    log_jni("[AUTO] CallNonvirtualLongMethodA(" "env=%p, obj=%p, clazz=%p, methodID=%p, args=%p "")", (void*)env, (void*)obj, (void*)clazz, (void*)methodID, (void*)args);
    return 0;
}

static jfloat JNICALL jni_CallNonvirtualFloatMethod(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, ...) {
    log_jni("[AUTO] CallNonvirtualFloatMethod(" "env=%p, obj=%p, clazz=%p, methodID=%p "")", (void*)env, (void*)obj, (void*)clazz, (void*)methodID);
    return 0;
}

static jfloat JNICALL jni_CallNonvirtualFloatMethodV(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, va_list args) {
    log_jni("[AUTO] CallNonvirtualFloatMethodV(" "env=%p, obj=%p, clazz=%p, methodID=%p, args=%p "")", (void*)env, (void*)obj, (void*)clazz, (void*)methodID, (void*)args);
    return 0;
}

static jfloat JNICALL jni_CallNonvirtualFloatMethodA(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, const jvalue *args) {
    log_jni("[AUTO] CallNonvirtualFloatMethodA(" "env=%p, obj=%p, clazz=%p, methodID=%p, args=%p "")", (void*)env, (void*)obj, (void*)clazz, (void*)methodID, (void*)args);
    return 0;
}

static jdouble JNICALL jni_CallNonvirtualDoubleMethod(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, ...) {
    log_jni("[AUTO] CallNonvirtualDoubleMethod(" "env=%p, obj=%p, clazz=%p, methodID=%p "")", (void*)env, (void*)obj, (void*)clazz, (void*)methodID);
    return 0;
}

static jdouble JNICALL jni_CallNonvirtualDoubleMethodV(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, va_list args) {
    log_jni("[AUTO] CallNonvirtualDoubleMethodV(" "env=%p, obj=%p, clazz=%p, methodID=%p, args=%p "")", (void*)env, (void*)obj, (void*)clazz, (void*)methodID, (void*)args);
    return 0;
}

static jdouble JNICALL jni_CallNonvirtualDoubleMethodA(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, const jvalue *args) {
    log_jni("[AUTO] CallNonvirtualDoubleMethodA(" "env=%p, obj=%p, clazz=%p, methodID=%p, args=%p "")", (void*)env, (void*)obj, (void*)clazz, (void*)methodID, (void*)args);
    return 0;
}

static void JNICALL jni_CallNonvirtualVoidMethod(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, ...) {
    log_jni("[AUTO] CallNonvirtualVoidMethod(" "env=%p, obj=%p, clazz=%p, methodID=%p "")", (void*)env, (void*)obj, (void*)clazz, (void*)methodID);
    return;
}

static void JNICALL jni_CallNonvirtualVoidMethodV(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, va_list args) {
    log_jni("[AUTO] CallNonvirtualVoidMethodV(" "env=%p, obj=%p, clazz=%p, methodID=%p, args=%p "")", (void*)env, (void*)obj, (void*)clazz, (void*)methodID, (void*)args);
    return;
}

static void JNICALL jni_CallNonvirtualVoidMethodA(JNIEnv *env, jobject obj, jclass clazz, jmethodID methodID, const jvalue * args) {
    log_jni("[AUTO] CallNonvirtualVoidMethodA(" "env=%p, obj=%p, clazz=%p, methodID=%p, args=%p "")", (void*)env, (void*)obj, (void*)clazz, (void*)methodID, (void*)args);
    return;
}

static jfieldID JNICALL jni_GetFieldID(JNIEnv *env, jclass clazz, const char *name, const char *sig) {
    log_jni("[AUTO] GetFieldID(" "env=%p, clazz=%p, name=%p, sig=%p "")", (void*)env, (void*)clazz, (void*)name, (void*)sig);
    return nullptr;
}

static jobject JNICALL jni_GetObjectField(JNIEnv *env, jobject obj, jfieldID fieldID) {
    log_jni("[AUTO] GetObjectField(" "env=%p, obj=%p, fieldID=%p "")", (void*)env, (void*)obj, (void*)fieldID);
    return nullptr;
}

static jboolean JNICALL jni_GetBooleanField(JNIEnv *env, jobject obj, jfieldID fieldID) {
    log_jni("[AUTO] GetBooleanField(" "env=%p, obj=%p, fieldID=%p "")", (void*)env, (void*)obj, (void*)fieldID);
    return JNI_FALSE;
}

static jbyte JNICALL jni_GetByteField(JNIEnv *env, jobject obj, jfieldID fieldID) {
    log_jni("[AUTO] GetByteField(" "env=%p, obj=%p, fieldID=%p "")", (void*)env, (void*)obj, (void*)fieldID);
    return 0;
}

static jchar JNICALL jni_GetCharField(JNIEnv *env, jobject obj, jfieldID fieldID) {
    log_jni("[AUTO] GetCharField(" "env=%p, obj=%p, fieldID=%p "")", (void*)env, (void*)obj, (void*)fieldID);
    return 0;
}

static jshort JNICALL jni_GetShortField(JNIEnv *env, jobject obj, jfieldID fieldID) {
    log_jni("[AUTO] GetShortField(" "env=%p, obj=%p, fieldID=%p "")", (void*)env, (void*)obj, (void*)fieldID);
    return 0;
}

static jint JNICALL jni_GetIntField(JNIEnv *env, jobject obj, jfieldID fieldID) {
    log_jni("[AUTO] GetIntField(" "env=%p, obj=%p, fieldID=%p "")", (void*)env, (void*)obj, (void*)fieldID);
    return 0;
}

static jlong JNICALL jni_GetLongField(JNIEnv *env, jobject obj, jfieldID fieldID) {
    log_jni("[AUTO] GetLongField(" "env=%p, obj=%p, fieldID=%p "")", (void*)env, (void*)obj, (void*)fieldID);
    return 0;
}

static jfloat JNICALL jni_GetFloatField(JNIEnv *env, jobject obj, jfieldID fieldID) {
    log_jni("[AUTO] GetFloatField(" "env=%p, obj=%p, fieldID=%p "")", (void*)env, (void*)obj, (void*)fieldID);
    return 0;
}

static jdouble JNICALL jni_GetDoubleField(JNIEnv *env, jobject obj, jfieldID fieldID) {
    log_jni("[AUTO] GetDoubleField(" "env=%p, obj=%p, fieldID=%p "")", (void*)env, (void*)obj, (void*)fieldID);
    return 0;
}

static void JNICALL jni_SetObjectField(JNIEnv *env, jobject obj, jfieldID fieldID, jobject val) {
    log_jni("[AUTO] SetObjectField(" "env=%p, obj=%p, fieldID=%p, val=%p "")", (void*)env, (void*)obj, (void*)fieldID, (void*)val);
    return;
}

static void JNICALL jni_SetBooleanField(JNIEnv *env, jobject obj, jfieldID fieldID, jboolean val) {
    log_jni("[AUTO] SetBooleanField(" "env=%p, obj=%p, fieldID=%p, val=%d "")", (void*)env, (void*)obj, (void*)fieldID, (int)val);
    return;
}

static void JNICALL jni_SetByteField(JNIEnv *env, jobject obj, jfieldID fieldID, jbyte val) {
    log_jni("[AUTO] SetByteField(" "env=%p, obj=%p, fieldID=%p, val=%d "")", (void*)env, (void*)obj, (void*)fieldID, (int)val);
    return;
}

static void JNICALL jni_SetCharField(JNIEnv *env, jobject obj, jfieldID fieldID, jchar val) {
    log_jni("[AUTO] SetCharField(" "env=%p, obj=%p, fieldID=%p, val=%d "")", (void*)env, (void*)obj, (void*)fieldID, (int)val);
    return;
}

static void JNICALL jni_SetShortField(JNIEnv *env, jobject obj, jfieldID fieldID, jshort val) {
    log_jni("[AUTO] SetShortField(" "env=%p, obj=%p, fieldID=%p, val=%d "")", (void*)env, (void*)obj, (void*)fieldID, (int)val);
    return;
}

static void JNICALL jni_SetIntField(JNIEnv *env, jobject obj, jfieldID fieldID, jint val) {
    log_jni("[AUTO] SetIntField(" "env=%p, obj=%p, fieldID=%p, val=%d "")", (void*)env, (void*)obj, (void*)fieldID, (int)val);
    return;
}

static void JNICALL jni_SetLongField(JNIEnv *env, jobject obj, jfieldID fieldID, jlong val) {
    log_jni("[AUTO] SetLongField(" "env=%p, obj=%p, fieldID=%p, val=%lld "")", (void*)env, (void*)obj, (void*)fieldID, (long long)val);
    return;
}

static void JNICALL jni_SetFloatField(JNIEnv *env, jobject obj, jfieldID fieldID, jfloat val) {
    log_jni("[AUTO] SetFloatField(" "env=%p, obj=%p, fieldID=%p, val=%f "")", (void*)env, (void*)obj, (void*)fieldID, (double)val);
    return;
}

static void JNICALL jni_SetDoubleField(JNIEnv *env, jobject obj, jfieldID fieldID, jdouble val) {
    log_jni("[AUTO] SetDoubleField(" "env=%p, obj=%p, fieldID=%p, val=%f "")", (void*)env, (void*)obj, (void*)fieldID, (double)val);
    return;
}

static jobject JNICALL jni_CallStaticObjectMethod(JNIEnv *env, jclass clazz, jmethodID methodID, ...) {
    log_jni("[AUTO] CallStaticObjectMethod(" "env=%p, clazz=%p, methodID=%p "")", (void*)env, (void*)clazz, (void*)methodID);
    return nullptr;
}

static jobject JNICALL jni_CallStaticObjectMethodV(JNIEnv *env, jclass clazz, jmethodID methodID, va_list args) {
    log_jni("[AUTO] CallStaticObjectMethodV(" "env=%p, clazz=%p, methodID=%p, args=%p "")", (void*)env, (void*)clazz, (void*)methodID, (void*)args);
    return nullptr;
}

static jobject JNICALL jni_CallStaticObjectMethodA(JNIEnv *env, jclass clazz, jmethodID methodID, const jvalue *args) {
    log_jni("[AUTO] CallStaticObjectMethodA(" "env=%p, clazz=%p, methodID=%p, args=%p "")", (void*)env, (void*)clazz, (void*)methodID, (void*)args);
    return nullptr;
}

static jboolean JNICALL jni_CallStaticBooleanMethod(JNIEnv *env, jclass clazz, jmethodID methodID, ...) {
    log_jni("[AUTO] CallStaticBooleanMethod(" "env=%p, clazz=%p, methodID=%p "")", (void*)env, (void*)clazz, (void*)methodID);
    return JNI_FALSE;
}

static jboolean JNICALL jni_CallStaticBooleanMethodV(JNIEnv *env, jclass clazz, jmethodID methodID, va_list args) {
    log_jni("[AUTO] CallStaticBooleanMethodV(" "env=%p, clazz=%p, methodID=%p, args=%p "")", (void*)env, (void*)clazz, (void*)methodID, (void*)args);
    return JNI_FALSE;
}

static jboolean JNICALL jni_CallStaticBooleanMethodA(JNIEnv *env, jclass clazz, jmethodID methodID, const jvalue *args) {
    log_jni("[AUTO] CallStaticBooleanMethodA(" "env=%p, clazz=%p, methodID=%p, args=%p "")", (void*)env, (void*)clazz, (void*)methodID, (void*)args);
    return JNI_FALSE;
}

static jbyte JNICALL jni_CallStaticByteMethod(JNIEnv *env, jclass clazz, jmethodID methodID, ...) {
    log_jni("[AUTO] CallStaticByteMethod(" "env=%p, clazz=%p, methodID=%p "")", (void*)env, (void*)clazz, (void*)methodID);
    return 0;
}

static jbyte JNICALL jni_CallStaticByteMethodV(JNIEnv *env, jclass clazz, jmethodID methodID, va_list args) {
    log_jni("[AUTO] CallStaticByteMethodV(" "env=%p, clazz=%p, methodID=%p, args=%p "")", (void*)env, (void*)clazz, (void*)methodID, (void*)args);
    return 0;
}

static jbyte JNICALL jni_CallStaticByteMethodA(JNIEnv *env, jclass clazz, jmethodID methodID, const jvalue *args) {
    log_jni("[AUTO] CallStaticByteMethodA(" "env=%p, clazz=%p, methodID=%p, args=%p "")", (void*)env, (void*)clazz, (void*)methodID, (void*)args);
    return 0;
}

static jchar JNICALL jni_CallStaticCharMethod(JNIEnv *env, jclass clazz, jmethodID methodID, ...) {
    log_jni("[AUTO] CallStaticCharMethod(" "env=%p, clazz=%p, methodID=%p "")", (void*)env, (void*)clazz, (void*)methodID);
    return 0;
}

static jchar JNICALL jni_CallStaticCharMethodV(JNIEnv *env, jclass clazz, jmethodID methodID, va_list args) {
    log_jni("[AUTO] CallStaticCharMethodV(" "env=%p, clazz=%p, methodID=%p, args=%p "")", (void*)env, (void*)clazz, (void*)methodID, (void*)args);
    return 0;
}

static jchar JNICALL jni_CallStaticCharMethodA(JNIEnv *env, jclass clazz, jmethodID methodID, const jvalue *args) {
    log_jni("[AUTO] CallStaticCharMethodA(" "env=%p, clazz=%p, methodID=%p, args=%p "")", (void*)env, (void*)clazz, (void*)methodID, (void*)args);
    return 0;
}

static jshort JNICALL jni_CallStaticShortMethod(JNIEnv *env, jclass clazz, jmethodID methodID, ...) {
    log_jni("[AUTO] CallStaticShortMethod(" "env=%p, clazz=%p, methodID=%p "")", (void*)env, (void*)clazz, (void*)methodID);
    return 0;
}

static jshort JNICALL jni_CallStaticShortMethodV(JNIEnv *env, jclass clazz, jmethodID methodID, va_list args) {
    log_jni("[AUTO] CallStaticShortMethodV(" "env=%p, clazz=%p, methodID=%p, args=%p "")", (void*)env, (void*)clazz, (void*)methodID, (void*)args);
    return 0;
}

static jshort JNICALL jni_CallStaticShortMethodA(JNIEnv *env, jclass clazz, jmethodID methodID, const jvalue *args) {
    log_jni("[AUTO] CallStaticShortMethodA(" "env=%p, clazz=%p, methodID=%p, args=%p "")", (void*)env, (void*)clazz, (void*)methodID, (void*)args);
    return 0;
}

static jint JNICALL jni_CallStaticIntMethod(JNIEnv *env, jclass clazz, jmethodID methodID, ...) {
    log_jni("[AUTO] CallStaticIntMethod(" "env=%p, clazz=%p, methodID=%p "")", (void*)env, (void*)clazz, (void*)methodID);
    return 0;
}

static jint JNICALL jni_CallStaticIntMethodV(JNIEnv *env, jclass clazz, jmethodID methodID, va_list args) {
    log_jni("[AUTO] CallStaticIntMethodV(" "env=%p, clazz=%p, methodID=%p, args=%p "")", (void*)env, (void*)clazz, (void*)methodID, (void*)args);
    return 0;
}

static jint JNICALL jni_CallStaticIntMethodA(JNIEnv *env, jclass clazz, jmethodID methodID, const jvalue *args) {
    log_jni("[AUTO] CallStaticIntMethodA(" "env=%p, clazz=%p, methodID=%p, args=%p "")", (void*)env, (void*)clazz, (void*)methodID, (void*)args);
    return 0;
}

static jlong JNICALL jni_CallStaticLongMethod(JNIEnv *env, jclass clazz, jmethodID methodID, ...) {
    log_jni("[AUTO] CallStaticLongMethod(" "env=%p, clazz=%p, methodID=%p "")", (void*)env, (void*)clazz, (void*)methodID);
    return 0;
}

static jlong JNICALL jni_CallStaticLongMethodV(JNIEnv *env, jclass clazz, jmethodID methodID, va_list args) {
    log_jni("[AUTO] CallStaticLongMethodV(" "env=%p, clazz=%p, methodID=%p, args=%p "")", (void*)env, (void*)clazz, (void*)methodID, (void*)args);
    return 0;
}

static jlong JNICALL jni_CallStaticLongMethodA(JNIEnv *env, jclass clazz, jmethodID methodID, const jvalue *args) {
    log_jni("[AUTO] CallStaticLongMethodA(" "env=%p, clazz=%p, methodID=%p, args=%p "")", (void*)env, (void*)clazz, (void*)methodID, (void*)args);
    return 0;
}

static jfloat JNICALL jni_CallStaticFloatMethod(JNIEnv *env, jclass clazz, jmethodID methodID, ...) {
    log_jni("[AUTO] CallStaticFloatMethod(" "env=%p, clazz=%p, methodID=%p "")", (void*)env, (void*)clazz, (void*)methodID);
    return 0;
}

static jfloat JNICALL jni_CallStaticFloatMethodV(JNIEnv *env, jclass clazz, jmethodID methodID, va_list args) {
    log_jni("[AUTO] CallStaticFloatMethodV(" "env=%p, clazz=%p, methodID=%p, args=%p "")", (void*)env, (void*)clazz, (void*)methodID, (void*)args);
    return 0;
}

static jfloat JNICALL jni_CallStaticFloatMethodA(JNIEnv *env, jclass clazz, jmethodID methodID, const jvalue *args) {
    log_jni("[AUTO] CallStaticFloatMethodA(" "env=%p, clazz=%p, methodID=%p, args=%p "")", (void*)env, (void*)clazz, (void*)methodID, (void*)args);
    return 0;
}

static jdouble JNICALL jni_CallStaticDoubleMethod(JNIEnv *env, jclass clazz, jmethodID methodID, ...) {
    log_jni("[AUTO] CallStaticDoubleMethod(" "env=%p, clazz=%p, methodID=%p "")", (void*)env, (void*)clazz, (void*)methodID);
    return 0;
}

static jdouble JNICALL jni_CallStaticDoubleMethodV(JNIEnv *env, jclass clazz, jmethodID methodID, va_list args) {
    log_jni("[AUTO] CallStaticDoubleMethodV(" "env=%p, clazz=%p, methodID=%p, args=%p "")", (void*)env, (void*)clazz, (void*)methodID, (void*)args);
    return 0;
}

static jdouble JNICALL jni_CallStaticDoubleMethodA(JNIEnv *env, jclass clazz, jmethodID methodID, const jvalue *args) {
    log_jni("[AUTO] CallStaticDoubleMethodA(" "env=%p, clazz=%p, methodID=%p, args=%p "")", (void*)env, (void*)clazz, (void*)methodID, (void*)args);
    return 0;
}

static void JNICALL jni_CallStaticVoidMethod(JNIEnv *env, jclass cls, jmethodID methodID, ...) {
    log_jni("[AUTO] CallStaticVoidMethod(" "env=%p, cls=%p, methodID=%p "")", (void*)env, (void*)cls, (void*)methodID);
    return;
}

static void JNICALL jni_CallStaticVoidMethodV(JNIEnv *env, jclass cls, jmethodID methodID, va_list args) {
    log_jni("[AUTO] CallStaticVoidMethodV(" "env=%p, cls=%p, methodID=%p, args=%p "")", (void*)env, (void*)cls, (void*)methodID, (void*)args);
    return;
}

static void JNICALL jni_CallStaticVoidMethodA(JNIEnv *env, jclass cls, jmethodID methodID, const jvalue * args) {
    log_jni("[AUTO] CallStaticVoidMethodA(" "env=%p, cls=%p, methodID=%p, args=%p "")", (void*)env, (void*)cls, (void*)methodID, (void*)args);
    return;
}

static jfieldID JNICALL jni_GetStaticFieldID(JNIEnv *env, jclass clazz, const char *name, const char *sig) {
    log_jni("[AUTO] GetStaticFieldID(" "env=%p, clazz=%p, name=%p, sig=%p "")", (void*)env, (void*)clazz, (void*)name, (void*)sig);
    return nullptr;
}

static jobject JNICALL jni_GetStaticObjectField(JNIEnv *env, jclass clazz, jfieldID fieldID) {
    log_jni("[AUTO] GetStaticObjectField(" "env=%p, clazz=%p, fieldID=%p "")", (void*)env, (void*)clazz, (void*)fieldID);
    return nullptr;
}

static jboolean JNICALL jni_GetStaticBooleanField(JNIEnv *env, jclass clazz, jfieldID fieldID) {
    log_jni("[AUTO] GetStaticBooleanField(" "env=%p, clazz=%p, fieldID=%p "")", (void*)env, (void*)clazz, (void*)fieldID);
    return JNI_FALSE;
}

static jbyte JNICALL jni_GetStaticByteField(JNIEnv *env, jclass clazz, jfieldID fieldID) {
    log_jni("[AUTO] GetStaticByteField(" "env=%p, clazz=%p, fieldID=%p "")", (void*)env, (void*)clazz, (void*)fieldID);
    return 0;
}

static jchar JNICALL jni_GetStaticCharField(JNIEnv *env, jclass clazz, jfieldID fieldID) {
    log_jni("[AUTO] GetStaticCharField(" "env=%p, clazz=%p, fieldID=%p "")", (void*)env, (void*)clazz, (void*)fieldID);
    return 0;
}

static jshort JNICALL jni_GetStaticShortField(JNIEnv *env, jclass clazz, jfieldID fieldID) {
    log_jni("[AUTO] GetStaticShortField(" "env=%p, clazz=%p, fieldID=%p "")", (void*)env, (void*)clazz, (void*)fieldID);
    return 0;
}

static jint JNICALL jni_GetStaticIntField(JNIEnv *env, jclass clazz, jfieldID fieldID) {
    log_jni("[AUTO] GetStaticIntField(" "env=%p, clazz=%p, fieldID=%p "")", (void*)env, (void*)clazz, (void*)fieldID);
    return 0;
}

static jlong JNICALL jni_GetStaticLongField(JNIEnv *env, jclass clazz, jfieldID fieldID) {
    log_jni("[AUTO] GetStaticLongField(" "env=%p, clazz=%p, fieldID=%p "")", (void*)env, (void*)clazz, (void*)fieldID);
    return 0;
}

static jfloat JNICALL jni_GetStaticFloatField(JNIEnv *env, jclass clazz, jfieldID fieldID) {
    log_jni("[AUTO] GetStaticFloatField(" "env=%p, clazz=%p, fieldID=%p "")", (void*)env, (void*)clazz, (void*)fieldID);
    return 0;
}

static jdouble JNICALL jni_GetStaticDoubleField(JNIEnv *env, jclass clazz, jfieldID fieldID) {
    log_jni("[AUTO] GetStaticDoubleField(" "env=%p, clazz=%p, fieldID=%p "")", (void*)env, (void*)clazz, (void*)fieldID);
    return 0;
}

static void JNICALL jni_SetStaticObjectField(JNIEnv *env, jclass clazz, jfieldID fieldID, jobject value) {
    log_jni("[AUTO] SetStaticObjectField(" "env=%p, clazz=%p, fieldID=%p, value=%p "")", (void*)env, (void*)clazz, (void*)fieldID, (void*)value);
    return;
}

static void JNICALL jni_SetStaticBooleanField(JNIEnv *env, jclass clazz, jfieldID fieldID, jboolean value) {
    log_jni("[AUTO] SetStaticBooleanField(" "env=%p, clazz=%p, fieldID=%p, value=%d "")", (void*)env, (void*)clazz, (void*)fieldID, (int)value);
    return;
}

static void JNICALL jni_SetStaticByteField(JNIEnv *env, jclass clazz, jfieldID fieldID, jbyte value) {
    log_jni("[AUTO] SetStaticByteField(" "env=%p, clazz=%p, fieldID=%p, value=%d "")", (void*)env, (void*)clazz, (void*)fieldID, (int)value);
    return;
}

static void JNICALL jni_SetStaticCharField(JNIEnv *env, jclass clazz, jfieldID fieldID, jchar value) {
    log_jni("[AUTO] SetStaticCharField(" "env=%p, clazz=%p, fieldID=%p, value=%d "")", (void*)env, (void*)clazz, (void*)fieldID, (int)value);
    return;
}

static void JNICALL jni_SetStaticShortField(JNIEnv *env, jclass clazz, jfieldID fieldID, jshort value) {
    log_jni("[AUTO] SetStaticShortField(" "env=%p, clazz=%p, fieldID=%p, value=%d "")", (void*)env, (void*)clazz, (void*)fieldID, (int)value);
    return;
}

static void JNICALL jni_SetStaticIntField(JNIEnv *env, jclass clazz, jfieldID fieldID, jint value) {
    log_jni("[AUTO] SetStaticIntField(" "env=%p, clazz=%p, fieldID=%p, value=%d "")", (void*)env, (void*)clazz, (void*)fieldID, (int)value);
    return;
}

static void JNICALL jni_SetStaticLongField(JNIEnv *env, jclass clazz, jfieldID fieldID, jlong value) {
    log_jni("[AUTO] SetStaticLongField(" "env=%p, clazz=%p, fieldID=%p, value=%lld "")", (void*)env, (void*)clazz, (void*)fieldID, (long long)value);
    return;
}

static void JNICALL jni_SetStaticFloatField(JNIEnv *env, jclass clazz, jfieldID fieldID, jfloat value) {
    log_jni("[AUTO] SetStaticFloatField(" "env=%p, clazz=%p, fieldID=%p, value=%f "")", (void*)env, (void*)clazz, (void*)fieldID, (double)value);
    return;
}

static void JNICALL jni_SetStaticDoubleField(JNIEnv *env, jclass clazz, jfieldID fieldID, jdouble value) {
    log_jni("[AUTO] SetStaticDoubleField(" "env=%p, clazz=%p, fieldID=%p, value=%f "")", (void*)env, (void*)clazz, (void*)fieldID, (double)value);
    return;
}

static jstring JNICALL jni_NewString(JNIEnv *env, const jchar *unicode, jsize len) {
    log_jni("[AUTO] NewString(" "env=%p, unicode=%d, len=%d "")", (void*)env, (int)unicode, (int)len);
    return nullptr;
}

static jsize JNICALL jni_GetStringLength(JNIEnv *env, jstring str) {
    log_jni("[AUTO] GetStringLength(" "env=%p, str=%p "")", (void*)env, (void*)str);
    return 0;
}

static const jchar * JNICALL jni_GetStringChars(JNIEnv *env, jstring str, jboolean *isCopy) {
    log_jni("[AUTO] GetStringChars(" "env=%p, str=%p, isCopy=%d "")", (void*)env, (void*)str, (int)isCopy);
    return nullptr;
}

static void JNICALL jni_ReleaseStringChars(JNIEnv *env, jstring str, const jchar *chars) {
    log_jni("[AUTO] ReleaseStringChars(" "env=%p, str=%p, chars=%d "")", (void*)env, (void*)str, (int)chars);
    return;
}

static jsize JNICALL jni_GetArrayLength(JNIEnv *env, jarray array) {
    log_jni("[AUTO] GetArrayLength(" "env=%p, array=%p "")", (void*)env, (void*)array);
    return 0;
}

static jobjectArray JNICALL jni_NewObjectArray(JNIEnv *env, jsize len, jclass clazz, jobject init) {
    log_jni("[AUTO] NewObjectArray(" "env=%p, len=%d, clazz=%p, init=%p "")", (void*)env, (int)len, (void*)clazz, (void*)init);
    return 0;
}

static jobject JNICALL jni_GetObjectArrayElement(JNIEnv *env, jobjectArray array, jsize index) {
    log_jni("[AUTO] GetObjectArrayElement(" "env=%p, array=%p, index=%d "")", (void*)env, (void*)array, (int)index);
    return nullptr;
}

static void JNICALL jni_SetObjectArrayElement(JNIEnv *env, jobjectArray array, jsize index, jobject val) {
    log_jni("[AUTO] SetObjectArrayElement(" "env=%p, array=%p, index=%d, val=%p "")", (void*)env, (void*)array, (int)index, (void*)val);
    return;
}

static jbooleanArray JNICALL jni_NewBooleanArray(JNIEnv *env, jsize len) {
    log_jni("[AUTO] NewBooleanArray(" "env=%p, len=%d "")", (void*)env, (int)len);
    return 0;
}

static jbyteArray JNICALL jni_NewByteArray(JNIEnv *env, jsize len) {
    log_jni("[AUTO] NewByteArray(" "env=%p, len=%d "")", (void*)env, (int)len);
    return 0;
}

static jcharArray JNICALL jni_NewCharArray(JNIEnv *env, jsize len) {
    log_jni("[AUTO] NewCharArray(" "env=%p, len=%d "")", (void*)env, (int)len);
    return 0;
}

static jshortArray JNICALL jni_NewShortArray(JNIEnv *env, jsize len) {
    log_jni("[AUTO] NewShortArray(" "env=%p, len=%d "")", (void*)env, (int)len);
    return 0;
}

static jintArray JNICALL jni_NewIntArray(JNIEnv *env, jsize len) {
    log_jni("[AUTO] NewIntArray(" "env=%p, len=%d "")", (void*)env, (int)len);
    return 0;
}

static jlongArray JNICALL jni_NewLongArray(JNIEnv *env, jsize len) {
    log_jni("[AUTO] NewLongArray(" "env=%p, len=%d "")", (void*)env, (int)len);
    return 0;
}

static jfloatArray JNICALL jni_NewFloatArray(JNIEnv *env, jsize len) {
    log_jni("[AUTO] NewFloatArray(" "env=%p, len=%d "")", (void*)env, (int)len);
    return 0;
}

static jdoubleArray JNICALL jni_NewDoubleArray(JNIEnv *env, jsize len) {
    log_jni("[AUTO] NewDoubleArray(" "env=%p, len=%d "")", (void*)env, (int)len);
    return 0;
}

static jboolean * JNICALL jni_GetBooleanArrayElements(JNIEnv *env, jbooleanArray array, jboolean *isCopy) {
    log_jni("[AUTO] GetBooleanArrayElements(" "env=%p, array=%d, isCopy=%d "")", (void*)env, (int)array, (int)isCopy);
    return nullptr;
}

static jbyte * JNICALL jni_GetByteArrayElements(JNIEnv *env, jbyteArray array, jboolean *isCopy) {
    log_jni("[AUTO] GetByteArrayElements(" "env=%p, array=%d, isCopy=%d "")", (void*)env, (int)array, (int)isCopy);
    return nullptr;
}

static jchar * JNICALL jni_GetCharArrayElements(JNIEnv *env, jcharArray array, jboolean *isCopy) {
    log_jni("[AUTO] GetCharArrayElements(" "env=%p, array=%d, isCopy=%d "")", (void*)env, (int)array, (int)isCopy);
    return nullptr;
}

static jshort * JNICALL jni_GetShortArrayElements(JNIEnv *env, jshortArray array, jboolean *isCopy) {
    log_jni("[AUTO] GetShortArrayElements(" "env=%p, array=%d, isCopy=%d "")", (void*)env, (int)array, (int)isCopy);
    return nullptr;
}

static jint * JNICALL jni_GetIntArrayElements(JNIEnv *env, jintArray array, jboolean *isCopy) {
    log_jni("[AUTO] GetIntArrayElements(" "env=%p, array=%d, isCopy=%d "")", (void*)env, (int)array, (int)isCopy);
    return nullptr;
}

static jlong * JNICALL jni_GetLongArrayElements(JNIEnv *env, jlongArray array, jboolean *isCopy) {
    log_jni("[AUTO] GetLongArrayElements(" "env=%p, array=%lld, isCopy=%d "")", (void*)env, (long long)array, (int)isCopy);
    return nullptr;
}

static jfloat * JNICALL jni_GetFloatArrayElements(JNIEnv *env, jfloatArray array, jboolean *isCopy) {
    log_jni("[AUTO] GetFloatArrayElements(" "env=%p, array=%f, isCopy=%d "")", (void*)env, (double)array, (int)isCopy);
    return nullptr;
}

static jdouble * JNICALL jni_GetDoubleArrayElements(JNIEnv *env, jdoubleArray array, jboolean *isCopy) {
    log_jni("[AUTO] GetDoubleArrayElements(" "env=%p, array=%f, isCopy=%d "")", (void*)env, (double)array, (int)isCopy);
    return nullptr;
}

static void JNICALL jni_ReleaseBooleanArrayElements(JNIEnv *env, jbooleanArray array, jboolean *elems, jint mode) {
    log_jni("[AUTO] ReleaseBooleanArrayElements(" "env=%p, array=%d, elems=%d, mode=%d "")", (void*)env, (int)array, (int)elems, (int)mode);
    return;
}

static void JNICALL jni_ReleaseByteArrayElements(JNIEnv *env, jbyteArray array, jbyte *elems, jint mode) {
    log_jni("[AUTO] ReleaseByteArrayElements(" "env=%p, array=%d, elems=%d, mode=%d "")", (void*)env, (int)array, (int)elems, (int)mode);
    return;
}

static void JNICALL jni_ReleaseCharArrayElements(JNIEnv *env, jcharArray array, jchar *elems, jint mode) {
    log_jni("[AUTO] ReleaseCharArrayElements(" "env=%p, array=%d, elems=%d, mode=%d "")", (void*)env, (int)array, (int)elems, (int)mode);
    return;
}

static void JNICALL jni_ReleaseShortArrayElements(JNIEnv *env, jshortArray array, jshort *elems, jint mode) {
    log_jni("[AUTO] ReleaseShortArrayElements(" "env=%p, array=%d, elems=%d, mode=%d "")", (void*)env, (int)array, (int)elems, (int)mode);
    return;
}

static void JNICALL jni_ReleaseIntArrayElements(JNIEnv *env, jintArray array, jint *elems, jint mode) {
    log_jni("[AUTO] ReleaseIntArrayElements(" "env=%p, array=%d, elems=%d, mode=%d "")", (void*)env, (int)array, (int)elems, (int)mode);
    return;
}

static void JNICALL jni_ReleaseLongArrayElements(JNIEnv *env, jlongArray array, jlong *elems, jint mode) {
    log_jni("[AUTO] ReleaseLongArrayElements(" "env=%p, array=%lld, elems=%lld, mode=%d "")", (void*)env, (long long)array, (long long)elems, (int)mode);
    return;
}

static void JNICALL jni_ReleaseFloatArrayElements(JNIEnv *env, jfloatArray array, jfloat *elems, jint mode) {
    log_jni("[AUTO] ReleaseFloatArrayElements(" "env=%p, array=%f, elems=%f, mode=%d "")", (void*)env, (double)array, (double)elems, (int)mode);
    return;
}

static void JNICALL jni_ReleaseDoubleArrayElements(JNIEnv *env, jdoubleArray array, jdouble *elems, jint mode) {
    log_jni("[AUTO] ReleaseDoubleArrayElements(" "env=%p, array=%f, elems=%f, mode=%d "")", (void*)env, (double)array, (double)elems, (int)mode);
    return;
}

static void JNICALL jni_GetBooleanArrayRegion(JNIEnv *env, jbooleanArray array, jsize start, jsize l, jboolean *buf) {
    log_jni("[AUTO] GetBooleanArrayRegion(" "env=%p, array=%d, start=%d, l=%d, buf=%d "")", (void*)env, (int)array, (int)start, (int)l, (int)buf);
    return;
}

static void JNICALL jni_GetByteArrayRegion(JNIEnv *env, jbyteArray array, jsize start, jsize len, jbyte *buf) {
    log_jni("[AUTO] GetByteArrayRegion(" "env=%p, array=%d, start=%d, len=%d, buf=%d "")", (void*)env, (int)array, (int)start, (int)len, (int)buf);
    return;
}

static void JNICALL jni_GetCharArrayRegion(JNIEnv *env, jcharArray array, jsize start, jsize len, jchar *buf) {
    log_jni("[AUTO] GetCharArrayRegion(" "env=%p, array=%d, start=%d, len=%d, buf=%d "")", (void*)env, (int)array, (int)start, (int)len, (int)buf);
    return;
}

static void JNICALL jni_GetShortArrayRegion(JNIEnv *env, jshortArray array, jsize start, jsize len, jshort *buf) {
    log_jni("[AUTO] GetShortArrayRegion(" "env=%p, array=%d, start=%d, len=%d, buf=%d "")", (void*)env, (int)array, (int)start, (int)len, (int)buf);
    return;
}

static void JNICALL jni_GetIntArrayRegion(JNIEnv *env, jintArray array, jsize start, jsize len, jint *buf) {
    log_jni("[AUTO] GetIntArrayRegion(" "env=%p, array=%d, start=%d, len=%d, buf=%d "")", (void*)env, (int)array, (int)start, (int)len, (int)buf);
    return;
}

static void JNICALL jni_GetLongArrayRegion(JNIEnv *env, jlongArray array, jsize start, jsize len, jlong *buf) {
    log_jni("[AUTO] GetLongArrayRegion(" "env=%p, array=%lld, start=%d, len=%d, buf=%lld "")", (void*)env, (long long)array, (int)start, (int)len, (long long)buf);
    return;
}

static void JNICALL jni_GetFloatArrayRegion(JNIEnv *env, jfloatArray array, jsize start, jsize len, jfloat *buf) {
    log_jni("[AUTO] GetFloatArrayRegion(" "env=%p, array=%f, start=%d, len=%d, buf=%f "")", (void*)env, (double)array, (int)start, (int)len, (double)buf);
    return;
}

static void JNICALL jni_GetDoubleArrayRegion(JNIEnv *env, jdoubleArray array, jsize start, jsize len, jdouble *buf) {
    log_jni("[AUTO] GetDoubleArrayRegion(" "env=%p, array=%f, start=%d, len=%d, buf=%f "")", (void*)env, (double)array, (int)start, (int)len, (double)buf);
    return;
}

static void JNICALL jni_SetBooleanArrayRegion(JNIEnv *env, jbooleanArray array, jsize start, jsize l, const jboolean *buf) {
    log_jni("[AUTO] SetBooleanArrayRegion(" "env=%p, array=%d, start=%d, l=%d, buf=%d "")", (void*)env, (int)array, (int)start, (int)l, (int)buf);
    return;
}

static void JNICALL jni_SetByteArrayRegion(JNIEnv *env, jbyteArray array, jsize start, jsize len, const jbyte *buf) {
    log_jni("[AUTO] SetByteArrayRegion(" "env=%p, array=%d, start=%d, len=%d, buf=%d "")", (void*)env, (int)array, (int)start, (int)len, (int)buf);
    return;
}

static void JNICALL jni_SetCharArrayRegion(JNIEnv *env, jcharArray array, jsize start, jsize len, const jchar *buf) {
    log_jni("[AUTO] SetCharArrayRegion(" "env=%p, array=%d, start=%d, len=%d, buf=%d "")", (void*)env, (int)array, (int)start, (int)len, (int)buf);
    return;
}

static void JNICALL jni_SetShortArrayRegion(JNIEnv *env, jshortArray array, jsize start, jsize len, const jshort *buf) {
    log_jni("[AUTO] SetShortArrayRegion(" "env=%p, array=%d, start=%d, len=%d, buf=%d "")", (void*)env, (int)array, (int)start, (int)len, (int)buf);
    return;
}

static void JNICALL jni_SetIntArrayRegion(JNIEnv *env, jintArray array, jsize start, jsize len, const jint *buf) {
    log_jni("[AUTO] SetIntArrayRegion(" "env=%p, array=%d, start=%d, len=%d, buf=%d "")", (void*)env, (int)array, (int)start, (int)len, (int)buf);
    return;
}

static void JNICALL jni_SetLongArrayRegion(JNIEnv *env, jlongArray array, jsize start, jsize len, const jlong *buf) {
    log_jni("[AUTO] SetLongArrayRegion(" "env=%p, array=%lld, start=%d, len=%d, buf=%lld "")", (void*)env, (long long)array, (int)start, (int)len, (long long)buf);
    return;
}

static void JNICALL jni_SetFloatArrayRegion(JNIEnv *env, jfloatArray array, jsize start, jsize len, const jfloat *buf) {
    log_jni("[AUTO] SetFloatArrayRegion(" "env=%p, array=%f, start=%d, len=%d, buf=%f "")", (void*)env, (double)array, (int)start, (int)len, (double)buf);
    return;
}

static void JNICALL jni_SetDoubleArrayRegion(JNIEnv *env, jdoubleArray array, jsize start, jsize len, const jdouble *buf) {
    log_jni("[AUTO] SetDoubleArrayRegion(" "env=%p, array=%f, start=%d, len=%d, buf=%f "")", (void*)env, (double)array, (int)start, (int)len, (double)buf);
    return;
}

static jint JNICALL jni_UnregisterNatives(JNIEnv *env, jclass clazz) {
    log_jni("[AUTO] UnregisterNatives(" "env=%p, clazz=%p "")", (void*)env, (void*)clazz);
    return 0;
}

static jint JNICALL jni_MonitorEnter(JNIEnv *env, jobject obj) {
    log_jni("[AUTO] MonitorEnter(" "env=%p, obj=%p "")", (void*)env, (void*)obj);
    return 0;
}

static jint JNICALL jni_MonitorExit(JNIEnv *env, jobject obj) {
    log_jni("[AUTO] MonitorExit(" "env=%p, obj=%p "")", (void*)env, (void*)obj);
    return 0;
}

static jint JNICALL jni_GetJavaVM(JNIEnv *env, JavaVM **vm) {
    log_jni("[AUTO] GetJavaVM(" "env=%p, vm=%p "")", (void*)env, (void*)vm);
    return 0;
}

static void JNICALL jni_GetStringRegion(JNIEnv *env, jstring str, jsize start, jsize len, jchar *buf) {
    log_jni("[AUTO] GetStringRegion(" "env=%p, str=%p, start=%d, len=%d, buf=%d "")", (void*)env, (void*)str, (int)start, (int)len, (int)buf);
    return;
}

static void JNICALL jni_GetStringUTFRegion(JNIEnv *env, jstring str, jsize start, jsize len, char *buf) {
    log_jni("[AUTO] GetStringUTFRegion(" "env=%p, str=%p, start=%d, len=%d, buf=%p "")", (void*)env, (void*)str, (int)start, (int)len, (void*)buf);
    return;
}

static void * JNICALL jni_GetPrimitiveArrayCritical(JNIEnv *env, jarray array, jboolean *isCopy) {
    log_jni("[AUTO] GetPrimitiveArrayCritical(" "env=%p, array=%p, isCopy=%d "")", (void*)env, (void*)array, (int)isCopy);
    return nullptr;
}

static void JNICALL jni_ReleasePrimitiveArrayCritical(JNIEnv *env, jarray array, void *carray, jint mode) {
    log_jni("[AUTO] ReleasePrimitiveArrayCritical(" "env=%p, array=%p, carray=%p, mode=%d "")", (void*)env, (void*)array, (void*)carray, (int)mode);
    return;
}

static const jchar * JNICALL jni_GetStringCritical(JNIEnv *env, jstring string, jboolean *isCopy) {
    log_jni("[AUTO] GetStringCritical(" "env=%p, string=%p, isCopy=%d "")", (void*)env, (void*)string, (int)isCopy);
    return nullptr;
}

static void JNICALL jni_ReleaseStringCritical(JNIEnv *env, jstring string, const jchar *cstring) {
    log_jni("[AUTO] ReleaseStringCritical(" "env=%p, string=%p, cstring=%d "")", (void*)env, (void*)string, (int)cstring);
    return;
}

static jweak JNICALL jni_NewWeakGlobalRef(JNIEnv *env, jobject obj) {
    log_jni("[AUTO] NewWeakGlobalRef(" "env=%p, obj=%p "")", (void*)env, (void*)obj);
    return 0;
}

static void JNICALL jni_DeleteWeakGlobalRef(JNIEnv *env, jweak ref) {
    log_jni("[AUTO] DeleteWeakGlobalRef(" "env=%p, ref=%p "")", (void*)env, (void*)ref);
    return;
}

static jboolean JNICALL jni_ExceptionCheck(JNIEnv *env) {
    log_jni("[AUTO] ExceptionCheck(" "env=%p "")", (void*)env);
    return JNI_FALSE;
}

static jobject JNICALL jni_NewDirectByteBuffer(JNIEnv* env, void* address, jlong capacity) {
    log_jni("[AUTO] NewDirectByteBuffer(" "env=%p, address=%p, capacity=%lld "")", (void*)env, (void*)address, (long long)capacity);
    return nullptr;
}

static void* JNICALL jni_GetDirectBufferAddress(JNIEnv* env, jobject buf) {
    log_jni("[AUTO] GetDirectBufferAddress(" "env=%p, buf=%p "")", (void*)env, (void*)buf);
    return nullptr;
}

static jlong JNICALL jni_GetDirectBufferCapacity(JNIEnv* env, jobject buf) {
    log_jni("[AUTO] GetDirectBufferCapacity(" "env=%p, buf=%p "")", (void*)env, (void*)buf);
    return 0;
}

static jobjectRefType JNICALL jni_GetObjectRefType(JNIEnv* env, jobject obj) {
    log_jni("[AUTO] GetObjectRefType(" "env=%p, obj=%p "")", (void*)env, (void*)obj);
    return JNIInvalidRefType;
}

static jobject JNICALL jni_GetModule(JNIEnv* env, jclass clazz) {
    log_jni("[AUTO] GetModule(" "env=%p, clazz=%p "")", (void*)env, (void*)clazz);
    return nullptr;
}

static jboolean JNICALL jni_IsVirtualThread(JNIEnv* env, jobject obj) {
    log_jni("[AUTO] IsVirtualThread(" "env=%p, obj=%p "")", (void*)env, (void*)obj);
    return JNI_FALSE;
}

static jlong JNICALL jni_GetStringUTFLengthAsLong(JNIEnv *env, jstring str) {
    log_jni("[AUTO] GetStringUTFLengthAsLong(" "env=%p, str=%p "")", (void*)env, (void*)str);
    return 0;
}

static jboolean JNICALL jni_HasIdentity(JNIEnv* env, jobject obj) {
    log_jni("[AUTO] HasIdentity(" "env=%p, obj=%p "")", (void*)env, (void*)obj);
    return JNI_FALSE;
}

static void init_generated_jni_interface(JNINativeInterface_* iface) {
    iface->GetVersion = jni_GetVersion;
    iface->DefineClass = jni_DefineClass;
    iface->FindClass = jni_FindClass;
    iface->FromReflectedMethod = jni_FromReflectedMethod;
    iface->FromReflectedField = jni_FromReflectedField;
    iface->ToReflectedMethod = jni_ToReflectedMethod;
    iface->GetSuperclass = jni_GetSuperclass;
    iface->IsAssignableFrom = jni_IsAssignableFrom;
    iface->ToReflectedField = jni_ToReflectedField;
    iface->Throw = jni_Throw;
    iface->ThrowNew = jni_ThrowNew;
    iface->ExceptionOccurred = jni_ExceptionOccurred;
    iface->ExceptionDescribe = jni_ExceptionDescribe;
    iface->ExceptionClear = jni_ExceptionClear;
    iface->FatalError = jni_FatalError;
    iface->PushLocalFrame = jni_PushLocalFrame;
    iface->PopLocalFrame = jni_PopLocalFrame;
    iface->NewGlobalRef = jni_NewGlobalRef;
    iface->DeleteGlobalRef = jni_DeleteGlobalRef;
    iface->DeleteLocalRef = jni_DeleteLocalRef;
    iface->IsSameObject = jni_IsSameObject;
    iface->NewLocalRef = jni_NewLocalRef;
    iface->EnsureLocalCapacity = jni_EnsureLocalCapacity;
    iface->AllocObject = jni_AllocObject;
    iface->NewObject = jni_NewObject;
    iface->NewObjectV = jni_NewObjectV;
    iface->NewObjectA = jni_NewObjectA;
    iface->GetObjectClass = jni_GetObjectClass;
    iface->IsInstanceOf = jni_IsInstanceOf;
    iface->GetMethodID = jni_GetMethodID;
    iface->CallObjectMethod = jni_CallObjectMethod;
    iface->CallObjectMethodV = jni_CallObjectMethodV;
    iface->CallObjectMethodA = jni_CallObjectMethodA;
    iface->CallBooleanMethod = jni_CallBooleanMethod;
    iface->CallBooleanMethodV = jni_CallBooleanMethodV;
    iface->CallBooleanMethodA = jni_CallBooleanMethodA;
    iface->CallByteMethod = jni_CallByteMethod;
    iface->CallByteMethodV = jni_CallByteMethodV;
    iface->CallByteMethodA = jni_CallByteMethodA;
    iface->CallCharMethod = jni_CallCharMethod;
    iface->CallCharMethodV = jni_CallCharMethodV;
    iface->CallCharMethodA = jni_CallCharMethodA;
    iface->CallShortMethod = jni_CallShortMethod;
    iface->CallShortMethodV = jni_CallShortMethodV;
    iface->CallShortMethodA = jni_CallShortMethodA;
    iface->CallIntMethod = jni_CallIntMethod;
    iface->CallIntMethodV = jni_CallIntMethodV;
    iface->CallIntMethodA = jni_CallIntMethodA;
    iface->CallLongMethod = jni_CallLongMethod;
    iface->CallLongMethodV = jni_CallLongMethodV;
    iface->CallLongMethodA = jni_CallLongMethodA;
    iface->CallFloatMethod = jni_CallFloatMethod;
    iface->CallFloatMethodV = jni_CallFloatMethodV;
    iface->CallFloatMethodA = jni_CallFloatMethodA;
    iface->CallDoubleMethod = jni_CallDoubleMethod;
    iface->CallDoubleMethodV = jni_CallDoubleMethodV;
    iface->CallDoubleMethodA = jni_CallDoubleMethodA;
    iface->CallVoidMethod = jni_CallVoidMethod;
    iface->CallVoidMethodV = jni_CallVoidMethodV;
    iface->CallVoidMethodA = jni_CallVoidMethodA;
    iface->CallNonvirtualObjectMethod = jni_CallNonvirtualObjectMethod;
    iface->CallNonvirtualObjectMethodV = jni_CallNonvirtualObjectMethodV;
    iface->CallNonvirtualObjectMethodA = jni_CallNonvirtualObjectMethodA;
    iface->CallNonvirtualBooleanMethod = jni_CallNonvirtualBooleanMethod;
    iface->CallNonvirtualBooleanMethodV = jni_CallNonvirtualBooleanMethodV;
    iface->CallNonvirtualBooleanMethodA = jni_CallNonvirtualBooleanMethodA;
    iface->CallNonvirtualByteMethod = jni_CallNonvirtualByteMethod;
    iface->CallNonvirtualByteMethodV = jni_CallNonvirtualByteMethodV;
    iface->CallNonvirtualByteMethodA = jni_CallNonvirtualByteMethodA;
    iface->CallNonvirtualCharMethod = jni_CallNonvirtualCharMethod;
    iface->CallNonvirtualCharMethodV = jni_CallNonvirtualCharMethodV;
    iface->CallNonvirtualCharMethodA = jni_CallNonvirtualCharMethodA;
    iface->CallNonvirtualShortMethod = jni_CallNonvirtualShortMethod;
    iface->CallNonvirtualShortMethodV = jni_CallNonvirtualShortMethodV;
    iface->CallNonvirtualShortMethodA = jni_CallNonvirtualShortMethodA;
    iface->CallNonvirtualIntMethod = jni_CallNonvirtualIntMethod;
    iface->CallNonvirtualIntMethodV = jni_CallNonvirtualIntMethodV;
    iface->CallNonvirtualIntMethodA = jni_CallNonvirtualIntMethodA;
    iface->CallNonvirtualLongMethod = jni_CallNonvirtualLongMethod;
    iface->CallNonvirtualLongMethodV = jni_CallNonvirtualLongMethodV;
    iface->CallNonvirtualLongMethodA = jni_CallNonvirtualLongMethodA;
    iface->CallNonvirtualFloatMethod = jni_CallNonvirtualFloatMethod;
    iface->CallNonvirtualFloatMethodV = jni_CallNonvirtualFloatMethodV;
    iface->CallNonvirtualFloatMethodA = jni_CallNonvirtualFloatMethodA;
    iface->CallNonvirtualDoubleMethod = jni_CallNonvirtualDoubleMethod;
    iface->CallNonvirtualDoubleMethodV = jni_CallNonvirtualDoubleMethodV;
    iface->CallNonvirtualDoubleMethodA = jni_CallNonvirtualDoubleMethodA;
    iface->CallNonvirtualVoidMethod = jni_CallNonvirtualVoidMethod;
    iface->CallNonvirtualVoidMethodV = jni_CallNonvirtualVoidMethodV;
    iface->CallNonvirtualVoidMethodA = jni_CallNonvirtualVoidMethodA;
    iface->GetFieldID = jni_GetFieldID;
    iface->GetObjectField = jni_GetObjectField;
    iface->GetBooleanField = jni_GetBooleanField;
    iface->GetByteField = jni_GetByteField;
    iface->GetCharField = jni_GetCharField;
    iface->GetShortField = jni_GetShortField;
    iface->GetIntField = jni_GetIntField;
    iface->GetLongField = jni_GetLongField;
    iface->GetFloatField = jni_GetFloatField;
    iface->GetDoubleField = jni_GetDoubleField;
    iface->SetObjectField = jni_SetObjectField;
    iface->SetBooleanField = jni_SetBooleanField;
    iface->SetByteField = jni_SetByteField;
    iface->SetCharField = jni_SetCharField;
    iface->SetShortField = jni_SetShortField;
    iface->SetIntField = jni_SetIntField;
    iface->SetLongField = jni_SetLongField;
    iface->SetFloatField = jni_SetFloatField;
    iface->SetDoubleField = jni_SetDoubleField;
    iface->GetStaticMethodID = jni_GetStaticMethodID;
    iface->CallStaticObjectMethod = jni_CallStaticObjectMethod;
    iface->CallStaticObjectMethodV = jni_CallStaticObjectMethodV;
    iface->CallStaticObjectMethodA = jni_CallStaticObjectMethodA;
    iface->CallStaticBooleanMethod = jni_CallStaticBooleanMethod;
    iface->CallStaticBooleanMethodV = jni_CallStaticBooleanMethodV;
    iface->CallStaticBooleanMethodA = jni_CallStaticBooleanMethodA;
    iface->CallStaticByteMethod = jni_CallStaticByteMethod;
    iface->CallStaticByteMethodV = jni_CallStaticByteMethodV;
    iface->CallStaticByteMethodA = jni_CallStaticByteMethodA;
    iface->CallStaticCharMethod = jni_CallStaticCharMethod;
    iface->CallStaticCharMethodV = jni_CallStaticCharMethodV;
    iface->CallStaticCharMethodA = jni_CallStaticCharMethodA;
    iface->CallStaticShortMethod = jni_CallStaticShortMethod;
    iface->CallStaticShortMethodV = jni_CallStaticShortMethodV;
    iface->CallStaticShortMethodA = jni_CallStaticShortMethodA;
    iface->CallStaticIntMethod = jni_CallStaticIntMethod;
    iface->CallStaticIntMethodV = jni_CallStaticIntMethodV;
    iface->CallStaticIntMethodA = jni_CallStaticIntMethodA;
    iface->CallStaticLongMethod = jni_CallStaticLongMethod;
    iface->CallStaticLongMethodV = jni_CallStaticLongMethodV;
    iface->CallStaticLongMethodA = jni_CallStaticLongMethodA;
    iface->CallStaticFloatMethod = jni_CallStaticFloatMethod;
    iface->CallStaticFloatMethodV = jni_CallStaticFloatMethodV;
    iface->CallStaticFloatMethodA = jni_CallStaticFloatMethodA;
    iface->CallStaticDoubleMethod = jni_CallStaticDoubleMethod;
    iface->CallStaticDoubleMethodV = jni_CallStaticDoubleMethodV;
    iface->CallStaticDoubleMethodA = jni_CallStaticDoubleMethodA;
    iface->CallStaticVoidMethod = jni_CallStaticVoidMethod;
    iface->CallStaticVoidMethodV = jni_CallStaticVoidMethodV;
    iface->CallStaticVoidMethodA = jni_CallStaticVoidMethodA;
    iface->GetStaticFieldID = jni_GetStaticFieldID;
    iface->GetStaticObjectField = jni_GetStaticObjectField;
    iface->GetStaticBooleanField = jni_GetStaticBooleanField;
    iface->GetStaticByteField = jni_GetStaticByteField;
    iface->GetStaticCharField = jni_GetStaticCharField;
    iface->GetStaticShortField = jni_GetStaticShortField;
    iface->GetStaticIntField = jni_GetStaticIntField;
    iface->GetStaticLongField = jni_GetStaticLongField;
    iface->GetStaticFloatField = jni_GetStaticFloatField;
    iface->GetStaticDoubleField = jni_GetStaticDoubleField;
    iface->SetStaticObjectField = jni_SetStaticObjectField;
    iface->SetStaticBooleanField = jni_SetStaticBooleanField;
    iface->SetStaticByteField = jni_SetStaticByteField;
    iface->SetStaticCharField = jni_SetStaticCharField;
    iface->SetStaticShortField = jni_SetStaticShortField;
    iface->SetStaticIntField = jni_SetStaticIntField;
    iface->SetStaticLongField = jni_SetStaticLongField;
    iface->SetStaticFloatField = jni_SetStaticFloatField;
    iface->SetStaticDoubleField = jni_SetStaticDoubleField;
    iface->NewString = jni_NewString;
    iface->GetStringLength = jni_GetStringLength;
    iface->GetStringChars = jni_GetStringChars;
    iface->ReleaseStringChars = jni_ReleaseStringChars;
    iface->NewStringUTF = jni_NewStringUTF;
    iface->GetStringUTFLength = jni_GetStringUTFLength;
    iface->GetStringUTFChars = jni_GetStringUTFChars;
    iface->ReleaseStringUTFChars = jni_ReleaseStringUTFChars;
    iface->GetArrayLength = jni_GetArrayLength;
    iface->NewObjectArray = jni_NewObjectArray;
    iface->GetObjectArrayElement = jni_GetObjectArrayElement;
    iface->SetObjectArrayElement = jni_SetObjectArrayElement;
    iface->NewBooleanArray = jni_NewBooleanArray;
    iface->NewByteArray = jni_NewByteArray;
    iface->NewCharArray = jni_NewCharArray;
    iface->NewShortArray = jni_NewShortArray;
    iface->NewIntArray = jni_NewIntArray;
    iface->NewLongArray = jni_NewLongArray;
    iface->NewFloatArray = jni_NewFloatArray;
    iface->NewDoubleArray = jni_NewDoubleArray;
    iface->GetBooleanArrayElements = jni_GetBooleanArrayElements;
    iface->GetByteArrayElements = jni_GetByteArrayElements;
    iface->GetCharArrayElements = jni_GetCharArrayElements;
    iface->GetShortArrayElements = jni_GetShortArrayElements;
    iface->GetIntArrayElements = jni_GetIntArrayElements;
    iface->GetLongArrayElements = jni_GetLongArrayElements;
    iface->GetFloatArrayElements = jni_GetFloatArrayElements;
    iface->GetDoubleArrayElements = jni_GetDoubleArrayElements;
    iface->ReleaseBooleanArrayElements = jni_ReleaseBooleanArrayElements;
    iface->ReleaseByteArrayElements = jni_ReleaseByteArrayElements;
    iface->ReleaseCharArrayElements = jni_ReleaseCharArrayElements;
    iface->ReleaseShortArrayElements = jni_ReleaseShortArrayElements;
    iface->ReleaseIntArrayElements = jni_ReleaseIntArrayElements;
    iface->ReleaseLongArrayElements = jni_ReleaseLongArrayElements;
    iface->ReleaseFloatArrayElements = jni_ReleaseFloatArrayElements;
    iface->ReleaseDoubleArrayElements = jni_ReleaseDoubleArrayElements;
    iface->GetBooleanArrayRegion = jni_GetBooleanArrayRegion;
    iface->GetByteArrayRegion = jni_GetByteArrayRegion;
    iface->GetCharArrayRegion = jni_GetCharArrayRegion;
    iface->GetShortArrayRegion = jni_GetShortArrayRegion;
    iface->GetIntArrayRegion = jni_GetIntArrayRegion;
    iface->GetLongArrayRegion = jni_GetLongArrayRegion;
    iface->GetFloatArrayRegion = jni_GetFloatArrayRegion;
    iface->GetDoubleArrayRegion = jni_GetDoubleArrayRegion;
    iface->SetBooleanArrayRegion = jni_SetBooleanArrayRegion;
    iface->SetByteArrayRegion = jni_SetByteArrayRegion;
    iface->SetCharArrayRegion = jni_SetCharArrayRegion;
    iface->SetShortArrayRegion = jni_SetShortArrayRegion;
    iface->SetIntArrayRegion = jni_SetIntArrayRegion;
    iface->SetLongArrayRegion = jni_SetLongArrayRegion;
    iface->SetFloatArrayRegion = jni_SetFloatArrayRegion;
    iface->SetDoubleArrayRegion = jni_SetDoubleArrayRegion;
    iface->RegisterNatives = jni_RegisterNatives;
    iface->UnregisterNatives = jni_UnregisterNatives;
    iface->MonitorEnter = jni_MonitorEnter;
    iface->MonitorExit = jni_MonitorExit;
    iface->GetJavaVM = jni_GetJavaVM;
    iface->GetStringRegion = jni_GetStringRegion;
    iface->GetStringUTFRegion = jni_GetStringUTFRegion;
    iface->GetPrimitiveArrayCritical = jni_GetPrimitiveArrayCritical;
    iface->ReleasePrimitiveArrayCritical = jni_ReleasePrimitiveArrayCritical;
    iface->GetStringCritical = jni_GetStringCritical;
    iface->ReleaseStringCritical = jni_ReleaseStringCritical;
    iface->NewWeakGlobalRef = jni_NewWeakGlobalRef;
    iface->DeleteWeakGlobalRef = jni_DeleteWeakGlobalRef;
    iface->ExceptionCheck = jni_ExceptionCheck;
    iface->NewDirectByteBuffer = jni_NewDirectByteBuffer;
    iface->GetDirectBufferAddress = jni_GetDirectBufferAddress;
    iface->GetDirectBufferCapacity = jni_GetDirectBufferCapacity;
    iface->GetObjectRefType = jni_GetObjectRefType;
    iface->GetModule = jni_GetModule;
    iface->IsVirtualThread = jni_IsVirtualThread;
    iface->GetStringUTFLengthAsLong = jni_GetStringUTFLengthAsLong;
    iface->HasIdentity = jni_HasIdentity;
}