// JNI function table; all entries delegate via DexJniEnv::FromEnv.
// Handle convention: jobject -> DexObject*, jclass -> DexClass*,
// jmethodID -> DexMethod*, jfieldID -> DexField*.
#include "kudroid/kuart/DexJniEnv.h"
#include "kudroid/abi/GuestVarargs.h"

#include <cstdlib>
#include <cstring>

#include <atomic>
#include <cstdio>

namespace kudroid {
namespace kuart {

// Namespace nested within anonymous namespace: still internal linkage but with name
// to qualify. Needed because in InitFunctionTable() non-qualified names will match
// member of DexJniEnv (PushLocalFrame, RegisterNatives, NewObjectA...) first.
namespace {
namespace jnifns {

DexJniEnv* Self(JNIEnv* env) { return DexJniEnv::FromEnv(env); }

DexObject* Obj(jobject o) { return reinterpret_cast<DexObject*>(o); }
DexClass* Cls(jclass c) { return reinterpret_cast<DexClass*>(c); }
DexMethod* Mth(jmethodID m) { return reinterpret_cast<DexMethod*>(m); }
DexField* Fld(jfieldID f) { return reinterpret_cast<DexField*>(f); }
DexString* Str(jstring s) { return reinterpret_cast<DexString*>(s); }
DexArray* Arr(jarray a) { return reinterpret_cast<DexArray*>(a); }

// Resolve a class from either handle form (jclass or heap Class).
DexClass* ClassOfHandle(DexJniEnv* self, DexObject* o) {
    if (o == nullptr || self == nullptr || self->linker() == nullptr) return nullptr;
    if (DexClass* represented = self->linker()->ClassFromObject(o)) return represented;
    const DexClass* k = reinterpret_cast<const DexClass*>(o);
    return self->linker()->IsRegisteredClass(k) ? const_cast<DexClass*>(k) : nullptr;
}

// Validate jclass against the linker; reject unknown handles with an exception.
DexClass* CheckedCls(DexJniEnv* self, jclass c) {
    DexClass* k = Cls(c);
    if (k == nullptr || self == nullptr) return nullptr;
    if (self->linker()->IsRegisteredClass(k)) return k;
    // Heap Class instance passed as jclass; resolve it rather than reject.
    if (DexClass* represented = self->linker()->ClassFromObject(Obj(c))) return represented;
    return nullptr;
}

jclass ToJClass(DexClass* k) { return reinterpret_cast<jclass>(k); }

// JNI uses "java/lang/String" type names and KuART uses descriptors
// "Ljava/lang/String;". The array name ("[I") is already a descriptor so it remains the same.
std::string ToDescriptor(const char* jni_name) {
    if (jni_name == nullptr) return std::string();
    if (jni_name[0] == '[') return jni_name;
    const size_t len = std::strlen(jni_name);
    if (len > 1 && jni_name[0] == 'L' && jni_name[len - 1] == ';') return jni_name;
    std::string d = "L";
    d += jni_name;
    d += ';';
    return d;
}

// Version, class, exception.

jint JNICALL GetVersion(JNIEnv*) { return JNI_VERSION_1_6; }

// Throw on failed lookup; silent nulls crash native callers that skip checks.
//
//     jclass c = env->FindClass("...");
//     jmethodID m = env->GetMethodID(c, "...", "...");   // c assumed valid
//     env->CallVoidMethod(obj, m);
void ThrowLookupFailure(DexJniEnv* self, const char* exception_descriptor,
                        const std::string& what) {
    if (self == nullptr) return;
    self->set_last_error(what);
    if (Interpreter* interp = self->interpreter()) {
        interp->ThrowException(exception_descriptor, what);
    }
}

jclass JNICALL FindClass(JNIEnv* env, const char* name) {
    DexJniEnv* self = Self(env);
    if (self == nullptr) return nullptr;
    if (name == nullptr) {
        ThrowLookupFailure(self, "Ljava/lang/NoClassDefFoundError;", "FindClass(null)");
        return nullptr;
    }
    const std::string descriptor = ToDescriptor(name);
    DexClass* klass = self->linker()->FindClass(descriptor.c_str());
    if (klass == nullptr) {
        if (JnibridgeTraceActive()) {
            std::fprintf(stderr, "[KuART][JNIBRIDGE] FindClass %s -> MISS\n", name);
        }
        ThrowLookupFailure(self, "Ljava/lang/NoClassDefFoundError;", name);
        return nullptr;
    }
    // A stub is a placeholder for a class KuDroid does not ship: it has no methods
    // and no fields, so handing it back would only move the failure to the next
    // GetMethodID, which returns null and takes the library down with no name
    // attached. Report the missing class here instead. See DexClass::is_stub.
    if (klass->is_stub) {
        if (JnibridgeTraceActive()) {
            std::fprintf(stderr, "[KuART][JNIBRIDGE] FindClass %s -> STUB\n", name);
        }
        ThrowLookupFailure(self, "Ljava/lang/NoClassDefFoundError;",
                           klass->PrettyName() +
                               " (class not implemented in KuDroid framework)");
        return nullptr;
    }
    // JNI FindClass instantiates the class (as defined by the JNI spec).
    if (self->interpreter() != nullptr) self->interpreter()->EnsureInitialized(klass);
    if (JnibridgeTraceActive()) {
        std::fprintf(stderr, "[KuART][JNIBRIDGE] FindClass %s -> FOUND\n", name);
    }
    return ToJClass(klass);
}

jclass JNICALL DefineClass(JNIEnv* env, const char*, jobject, const jbyte*, jsize) {
    // KuART does not load class from .class bytes; Android app uses DEX.
    if (DexJniEnv* self = Self(env)) self->set_last_error("DefineClass not supported");
    return nullptr;
}

jclass JNICALL GetSuperclass(JNIEnv* env, jclass sub) {
    DexClass* k = CheckedCls(Self(env), sub);
    return k != nullptr ? ToJClass(k->superclass) : nullptr;
}

jboolean JNICALL IsAssignableFrom(JNIEnv* env, jclass sub, jclass sup) {
    DexJniEnv* self = Self(env);
    DexClass* a = CheckedCls(self, sub);
    DexClass* b = CheckedCls(self, sup);
    if (a == nullptr || b == nullptr) return JNI_FALSE;
    const jboolean r = a->IsSubClassOf(b) ? JNI_TRUE : JNI_FALSE;
    if (JnibridgeTraceActive()) {
        std::fprintf(stderr, "[KuART][JNIBRIDGE] IsAssignableFrom %s -> %s = %d\n",
                     a->PrettyName().c_str(), b->PrettyName().c_str(), r);
    }
    return r;
}

jclass JNICALL GetObjectClass(JNIEnv* env, jobject obj) {
    DexJniEnv* self = Self(env);
    DexObject* o = Obj(obj);
    if (o == nullptr || self == nullptr) return nullptr;

    // A jclass shares offset 0 with DexObject::clazz; answer Class, do not misread.
    if (self->linker()->IsRegisteredClass(reinterpret_cast<const DexClass*>(o))) {
        if (JnibridgeTraceActive()) {
            std::fprintf(stderr, "[KuART][JNIBRIDGE] GetObjectClass -> java.lang.Class (jclass input)\n");
        }
        return ToJClass(self->linker()->FindClass("Ljava/lang/Class;"));
    }

    // Validate Class instances; stale handles return null.
    DexClass* result = self->linker()->ClassOfObject(o);
    if (JnibridgeTraceActive()) {
        std::fprintf(stderr, "[KuART][JNIBRIDGE] GetObjectClass -> %s\n",
                     result != nullptr ? result->PrettyName().c_str() : "(null)");
    }
    return ToJClass(result);
}

jboolean JNICALL IsInstanceOf(JNIEnv* env, jobject obj, jclass clazz) {
    DexJniEnv* self = Self(env);
    DexObject* o = Obj(obj);
    DexClass* k = CheckedCls(self, clazz);
    if (o == nullptr) return JNI_TRUE;  // null is an instance of every type
    if (k == nullptr || self == nullptr) return JNI_FALSE;
    DexClass* o_class = self->linker()->ClassOfObject(o);
    if (o_class == nullptr) return JNI_FALSE;
    const jboolean r = o_class->IsSubClassOf(k) ? JNI_TRUE : JNI_FALSE;
    if (JnibridgeTraceActive()) {
        std::fprintf(stderr, "[KuART][JNIBRIDGE] IsInstanceOf %s -> %s = %d\n",
                     o_class->PrettyName().c_str(), k->PrettyName().c_str(), r);
    }
    return r;
}

jint JNICALL Throw(JNIEnv* env, jthrowable obj) {
    DexJniEnv* self = Self(env);
    if (self == nullptr) return JNI_ERR;
    if (JnibridgeTraceActive()) {
        DexObject* ex = Obj(obj);
        std::fprintf(stderr, "[KuART][JNIBRIDGE] Throw %s\n",
                     (ex != nullptr && ex->clazz != nullptr)
                         ? ex->clazz->PrettyName().c_str()
                         : "(null)");
    }
    self->SetPendingException(Obj(obj));
    return JNI_OK;
}

jint JNICALL ThrowNew(JNIEnv* env, jclass clazz, const char* msg) {
    DexJniEnv* self = Self(env);
    if (self == nullptr) return JNI_ERR;
    DexClass* k = CheckedCls(self, clazz);
    if (k == nullptr) return JNI_ERR;
    DexObject* ex = self->linker()->AllocObject(k);
    if (ex == nullptr) return JNI_ERR;
    self->SetPendingException(ex);
    self->set_last_error(k->PrettyName() + ": " + (msg != nullptr ? msg : ""));
    if (JnibridgeTraceActive()) {
        std::fprintf(stderr, "[KuART][JNIBRIDGE] ThrowNew %s: %s\n",
                     k->PrettyName().c_str(), msg != nullptr ? msg : "");
    }
    return JNI_OK;
}

jthrowable JNICALL ExceptionOccurred(JNIEnv* env) {
    DexJniEnv* self = Self(env);
    return self != nullptr ? reinterpret_cast<jthrowable>(self->pending_exception()) : nullptr;
}

void JNICALL ExceptionDescribe(JNIEnv* env) {
    DexJniEnv* self = Self(env);
    if (self == nullptr) return;
    if (DexObject* ex = self->pending_exception()) {
        std::fprintf(stderr, "[KuART] exception: %s (%s)\n",
                     ex->clazz != nullptr ? ex->clazz->PrettyName().c_str() : "?",
                     self->last_error().c_str());
    }
}

void JNICALL ExceptionClear(JNIEnv* env) {
    if (DexJniEnv* self = Self(env)) {
        // Log the cleared exception; it names the real missing API.
        if (DexObject* ex = self->pending_exception()) {
            // Print both error slots to identify the thrower.
            const char* interp_err = "";
            if (Interpreter* interp = self->interpreter()) {
                interp_err = interp->last_error().c_str();
            }
            std::fprintf(stderr, "[KuART][JNI] ExceptionClear dropping %s (env: %s | interp: %s)\n",
                         ex->clazz != nullptr ? ex->clazz->PrettyName().c_str() : "?",
                         self->last_error().c_str(), interp_err);
        }
        self->ClearException();
    }
}

jboolean JNICALL ExceptionCheck(JNIEnv* env) {
    DexJniEnv* self = Self(env);
    const jboolean r =
        (self != nullptr && self->pending_exception() != nullptr) ? JNI_TRUE : JNI_FALSE;
    if (JnibridgeTraceActive()) {
        std::fprintf(stderr, "[KuART][JNIBRIDGE] ExceptionCheck -> %d\n", r);
    }
    return r;
}

void JNICALL FatalError(JNIEnv*, const char* msg) {
    std::fprintf(stderr, "[KuART] FatalError: %s\n", msg != nullptr ? msg : "");
    std::abort();
}

// Reference.

jint JNICALL PushLocalFrame(JNIEnv* env, jint) {
    if (DexJniEnv* self = Self(env)) self->PushLocalFrame();
    return JNI_OK;
}

jobject JNICALL PopLocalFrame(JNIEnv* env, jobject result) {
    DexJniEnv* self = Self(env);
    if (self == nullptr) return nullptr;
    self->PopLocalFrame();
    // The result must live in the CHA frame so add it again after pop.
    return result != nullptr ? self->AddLocalRef(Obj(result)) : nullptr;
}

jobject JNICALL NewGlobalRef(JNIEnv* env, jobject obj) {
    DexJniEnv* self = Self(env);
    return self != nullptr ? self->AddGlobalRef(Obj(obj)) : nullptr;
}

void JNICALL DeleteGlobalRef(JNIEnv* env, jobject ref) {
    if (DexJniEnv* self = Self(env)) self->DeleteGlobalRef(ref);
}

void JNICALL DeleteLocalRef(JNIEnv* env, jobject ref) {
    if (DexJniEnv* self = Self(env)) self->DeleteLocalRef(ref);
}

jobject JNICALL NewLocalRef(JNIEnv* env, jobject ref) {
    DexJniEnv* self = Self(env);
    return self != nullptr ? self->AddLocalRef(Obj(ref)) : nullptr;
}

jboolean JNICALL IsSameObject(JNIEnv* env, jobject a, jobject b) {
    DexObject* oa = Obj(a);
    DexObject* ob = Obj(b);
    jboolean r = JNI_FALSE;
    if (oa == ob) {
        r = JNI_TRUE;
    } else if (DexJniEnv* self = Self(env)) {
        // Same class under the two handle forms (see ClassOfHandle): a raw
        // pointer comparison is not the whole answer on KuART.
        DexClass* ka = ClassOfHandle(self, oa);
        DexClass* kb = ClassOfHandle(self, ob);
        if (ka != nullptr && ka == kb) r = JNI_TRUE;
    }
    if (JnibridgeTraceActive()) {
        DexClass* ka = ClassOfHandle(Self(env), oa);
        DexClass* kb = ClassOfHandle(Self(env), ob);
        std::fprintf(stderr, "[KuART][JNIBRIDGE] IsSameObject a=%p[%s] b=%p[%s] -> %d\n",
                     reinterpret_cast<const void*>(oa),
                     ka != nullptr ? ka->PrettyName().c_str() : "?",
                     reinterpret_cast<const void*>(ob),
                     kb != nullptr ? kb->PrettyName().c_str() : "?",
                     r);
    }
    return r;
}

jint JNICALL EnsureLocalCapacity(JNIEnv*, jint) { return JNI_OK; }

jweak JNICALL NewWeakGlobalRef(JNIEnv* env, jobject obj) {
    // There is no GC so weak refs behave like global refs.
    DexJniEnv* self = Self(env);
    return self != nullptr ? reinterpret_cast<jweak>(self->AddGlobalRef(Obj(obj))) : nullptr;
}

void JNICALL DeleteWeakGlobalRef(JNIEnv* env, jweak ref) {
    if (DexJniEnv* self = Self(env)) self->DeleteGlobalRef(ref);
}

jobjectRefType JNICALL GetObjectRefType(JNIEnv* env, jobject obj) {
    DexJniEnv* self = Self(env);
    if (self == nullptr || obj == nullptr) return JNIInvalidRefType;
    return self->IsGlobalRef(Obj(obj)) ? JNIGlobalRefType : JNILocalRefType;
}

// Create object.

jobject JNICALL AllocObject(JNIEnv* env, jclass clazz) {
    DexJniEnv* self = Self(env);
    if (self == nullptr) return nullptr;
    DexClass* k = CheckedCls(self, clazz);
    if (k == nullptr) {
        ThrowLookupFailure(self, "Ljava/lang/InstantiationException;",
                           "AllocObject with an invalid class handle");
        return nullptr;
    }
    if (self->interpreter() != nullptr) self->interpreter()->EnsureInitialized(k);
    return self->AddLocalRef(self->linker()->AllocObject(k));
}

jobject JNICALL NewObjectA(JNIEnv* env, jclass clazz, jmethodID methodID, const jvalue* args) {
    DexJniEnv* self = Self(env);
    if (self == nullptr) return nullptr;
    DexClass* k = CheckedCls(self, clazz);
    if (k == nullptr) {
        ThrowLookupFailure(self, "Ljava/lang/InstantiationException;",
                           "NewObject with an invalid class handle");
        return nullptr;
    }
    return self->AddLocalRef(self->NewObjectA(k, Mth(methodID), args));
}

jobject JNICALL NewObjectV(JNIEnv* env, jclass clazz, jmethodID methodID, va_list args) {
    DexJniEnv* self = Self(env);
    if (self == nullptr || methodID == nullptr) return nullptr;
    DexClass* k = CheckedCls(self, clazz);
    if (k == nullptr) {
        ThrowLookupFailure(self, "Ljava/lang/InstantiationException;",
                           "NewObject with an invalid class handle");
        return nullptr;
    }
    DexObject* obj = self->linker()->AllocObject(k);
    if (obj == nullptr) return nullptr;
    DexMethod* method = Mth(methodID);
    const char* shorty = DexJniEnv::MethodShorty(method);
    jvalue boxed[64];
    if (kudroid::UnpackGuestVaListToJvalues(shorty, reinterpret_cast<const void*>(args), boxed, 64)) {
        self->CallJavaA(obj, method, boxed, /*virtual_dispatch=*/false);
    } else {
        self->CallJavaV(obj, method, args, /*virtual_dispatch=*/false);
    }
    return self->AddLocalRef(obj);
}

jobject JNICALL NewObject(JNIEnv* env, jclass clazz, jmethodID methodID, ...) {
    va_list args;
    va_start(args, methodID);
    jobject r = NewObjectV(env, clazz, methodID, args);
    va_end(args);
    return r;
}

// Method/field ID: failed lookups throw per JNI spec, never return silent null.

jmethodID JNICALL GetMethodID(JNIEnv* env, jclass clazz, const char* name, const char* sig) {
    DexJniEnv* self = Self(env);
    if (self == nullptr) return nullptr;
    DexClass* k = CheckedCls(self, clazz);
    if (k == nullptr || name == nullptr) {
        ThrowLookupFailure(self, "Ljava/lang/NoSuchMethodError;",
                           std::string("GetMethodID with an invalid class handle (name=") +
                               (name != nullptr ? name : "null") + ")");
        return nullptr;
    }
    DexMethod* m = k->FindVirtualMethod(name, sig);
    if (m == nullptr) m = k->FindDirectMethod(name, sig);
    if (m == nullptr) {
        if (JnibridgeTraceActive()) {
            std::fprintf(stderr, "[KuART][JNIBRIDGE] GetMethodID %s.%s%s -> MISS\n",
                         k->PrettyName().c_str(), name, sig != nullptr ? sig : "?");
        }
        ThrowLookupFailure(self, "Ljava/lang/NoSuchMethodError;",
                           k->PrettyName() + "." + name + (sig != nullptr ? sig : ""));
    } else if (JnibridgeTraceActive()) {
        std::fprintf(stderr, "[KuART][JNIBRIDGE] GetMethodID %s.%s%s -> FOUND in %s\n",
                     k->PrettyName().c_str(), name, sig != nullptr ? sig : "?",
                     m->declaring_class != nullptr
                         ? m->declaring_class->PrettyName().c_str()
                         : "?");
    }
    return reinterpret_cast<jmethodID>(m);
}

jmethodID JNICALL GetStaticMethodID(JNIEnv* env, jclass clazz, const char* name,
                                   const char* sig) {
    DexJniEnv* self = Self(env);
    if (self == nullptr) return nullptr;
    DexClass* k = CheckedCls(self, clazz);
    if (k == nullptr || name == nullptr) {
        ThrowLookupFailure(self, "Ljava/lang/NoSuchMethodError;",
                           std::string("GetStaticMethodID with an invalid class handle (name=") +
                               (name != nullptr ? name : "null") + ")");
        return nullptr;
    }
    DexMethod* m = k->FindDirectMethod(name, sig);
    if (m == nullptr) {
        if (JnibridgeTraceActive()) {
            std::fprintf(stderr, "[KuART][JNIBRIDGE] GetStaticMethodID %s.%s%s -> MISS\n",
                         k->PrettyName().c_str(), name, sig != nullptr ? sig : "?");
        }
        ThrowLookupFailure(self, "Ljava/lang/NoSuchMethodError;",
                           k->PrettyName() + "." + name + (sig != nullptr ? sig : ""));
    } else if (JnibridgeTraceActive()) {
        std::fprintf(stderr, "[KuART][JNIBRIDGE] GetStaticMethodID %s.%s%s -> FOUND\n",
                     k->PrettyName().c_str(), name, sig != nullptr ? sig : "?");
    }
    return reinterpret_cast<jmethodID>(m);
}

jfieldID JNICALL GetFieldID(JNIEnv* env, jclass clazz, const char* name, const char* sig) {
    DexJniEnv* self = Self(env);
    if (self == nullptr) return nullptr;
    DexClass* k = CheckedCls(self, clazz);
    if (k == nullptr || name == nullptr) {
        ThrowLookupFailure(self, "Ljava/lang/NoSuchFieldError;",
                           std::string("GetFieldID with an invalid class handle (name=") +
                               (name != nullptr ? name : "null") + ")");
        return nullptr;
    }
    DexField* f = k->FindInstanceField(name, sig);
    if (f == nullptr) {
        if (JnibridgeTraceActive()) {
            std::fprintf(stderr, "[KuART][JNIBRIDGE] GetFieldID %s.%s -> MISS\n",
                         k->PrettyName().c_str(), name);
        }
        ThrowLookupFailure(self, "Ljava/lang/NoSuchFieldError;",
                           k->PrettyName() + "." + name + " " +
                               (sig != nullptr ? sig : ""));
    } else if (JnibridgeTraceActive()) {
        std::fprintf(stderr, "[KuART][JNIBRIDGE] GetFieldID %s.%s -> FOUND\n",
                     k->PrettyName().c_str(), name);
    }
    return reinterpret_cast<jfieldID>(f);
}

jfieldID JNICALL GetStaticFieldID(JNIEnv* env, jclass clazz, const char* name, const char* sig) {
    DexJniEnv* self = Self(env);
    if (self == nullptr) return nullptr;
    DexClass* k = CheckedCls(self, clazz);
    if (k == nullptr || name == nullptr) {
        ThrowLookupFailure(self, "Ljava/lang/NoSuchFieldError;",
                           std::string("GetStaticFieldID with an invalid class handle (name=") +
                               (name != nullptr ? name : "null") + ")");
        return nullptr;
    }
    DexField* f = k->FindStaticField(name, sig);
    if (f == nullptr) {
        if (JnibridgeTraceActive()) {
            std::fprintf(stderr, "[KuART][JNIBRIDGE] GetStaticFieldID %s.%s -> MISS\n",
                         k->PrettyName().c_str(), name);
        }
        ThrowLookupFailure(self, "Ljava/lang/NoSuchFieldError;",
                           k->PrettyName() + "." + name + " " +
                               (sig != nullptr ? sig : ""));
    } else if (JnibridgeTraceActive()) {
        std::fprintf(stderr, "[KuART][JNIBRIDGE] GetStaticFieldID %s.%s -> FOUND\n",
                     k->PrettyName().c_str(), name);
    }
    return reinterpret_cast<jfieldID>(f);
}

// Call method instance.
// Three variants (..., V, A) of 10 return types x 3 groups (virtual, nonvirtual,
// static) = 90 functions. Macro spawns them to avoid 90 similar blocks.

#define DEXRT_CALL_BODY(RET_TYPE, FIELD, RECEIVER, METHOD, VIRTUAL, ARGS_CALL)      \
    DexJniEnv* self = Self(env);                                                    \
    if (self == nullptr) return RET_TYPE();                                          \
    const DexValue v = ARGS_CALL;                                                    \
    return static_cast<RET_TYPE>(v.FIELD);

// Virtual group: Call<Type>Method / V / A
#define DEXRT_DEFINE_CALL(NAME, RET_TYPE, FIELD)                                    \
    RET_TYPE JNICALL Call##NAME##MethodA(JNIEnv* env, jobject obj, jmethodID mid,   \
                                        const jvalue* args) {                       \
        DexJniEnv* self = Self(env);                                                \
        if (self == nullptr) return RET_TYPE();                                      \
        return static_cast<RET_TYPE>(self->CallJavaA(Obj(obj), Mth(mid), args, true).FIELD); \
    }                                                                                \
    RET_TYPE JNICALL Call##NAME##MethodV(JNIEnv* env, jobject obj, jmethodID mid,   \
                                        va_list args) {                             \
        DexJniEnv* self = Self(env);                                                \
        if (self == nullptr || mid == nullptr) return RET_TYPE();                   \
        DexMethod* method = Mth(mid);                                               \
        const char* shorty = DexJniEnv::MethodShorty(method);                       \
        jvalue boxed[64];                                                           \
        if (kudroid::UnpackGuestVaListToJvalues(shorty, reinterpret_cast<const void*>(args), boxed, 64)) { \
            return static_cast<RET_TYPE>(self->CallJavaA(Obj(obj), method, boxed, true).FIELD); \
        }                                                                            \
        return static_cast<RET_TYPE>(self->CallJavaV(Obj(obj), method, args, true).FIELD); \
    }                                                                                \
    RET_TYPE JNICALL Call##NAME##Method(JNIEnv* env, jobject obj, jmethodID mid, ...) { \
        va_list args;                                                                \
        va_start(args, mid);                                                         \
        RET_TYPE r = Call##NAME##MethodV(env, obj, mid, args);                       \
        va_end(args);                                                                \
        return r;                                                                    \
    }

// Nonvirtual group: call the specified method, DO NOT dispatch back to the receiver.
#define DEXRT_DEFINE_CALL_NONVIRTUAL(NAME, RET_TYPE, FIELD)                          \
    RET_TYPE JNICALL CallNonvirtual##NAME##MethodA(JNIEnv* env, jobject obj, jclass, \
                                                  jmethodID mid, const jvalue* args) { \
        DexJniEnv* self = Self(env);                                                  \
        if (self == nullptr) return RET_TYPE();                                        \
        return static_cast<RET_TYPE>(self->CallJavaA(Obj(obj), Mth(mid), args, false).FIELD); \
    }                                                                                  \
    RET_TYPE JNICALL CallNonvirtual##NAME##MethodV(JNIEnv* env, jobject obj, jclass,  \
                                                  jmethodID mid, va_list args) {      \
        DexJniEnv* self = Self(env);                                                  \
        if (self == nullptr || mid == nullptr) return RET_TYPE();                     \
        DexMethod* method = Mth(mid);                                                 \
        const char* shorty = DexJniEnv::MethodShorty(method);                         \
        jvalue boxed[64];                                                             \
        if (kudroid::UnpackGuestVaListToJvalues(shorty, reinterpret_cast<const void*>(args), boxed, 64)) { \
            return static_cast<RET_TYPE>(self->CallJavaA(Obj(obj), method, boxed, false).FIELD); \
        }                                                                              \
        return static_cast<RET_TYPE>(self->CallJavaV(Obj(obj), method, args, false).FIELD); \
    }                                                                                  \
    RET_TYPE JNICALL CallNonvirtual##NAME##Method(JNIEnv* env, jobject obj, jclass c, \
                                                 jmethodID mid, ...) {                \
        va_list args;                                                                  \
        va_start(args, mid);                                                           \
        RET_TYPE r = CallNonvirtual##NAME##MethodV(env, obj, c, mid, args);            \
        va_end(args);                                                                  \
        return r;                                                                      \
    }

// Static group: receiver null.
#define DEXRT_DEFINE_CALL_STATIC(NAME, RET_TYPE, FIELD)                              \
    RET_TYPE JNICALL CallStatic##NAME##MethodA(JNIEnv* env, jclass, jmethodID mid,  \
                                              const jvalue* args) {                  \
        DexJniEnv* self = Self(env);                                                 \
        if (self == nullptr) return RET_TYPE();                                       \
        return static_cast<RET_TYPE>(self->CallJavaA(nullptr, Mth(mid), args, false).FIELD); \
    }                                                                                 \
    RET_TYPE JNICALL CallStatic##NAME##MethodV(JNIEnv* env, jclass, jmethodID mid,   \
                                              va_list args) {                        \
        DexJniEnv* self = Self(env);                                                 \
        if (self == nullptr || mid == nullptr) return RET_TYPE();                     \
        DexMethod* method = Mth(mid);                                                 \
        const char* shorty = DexJniEnv::MethodShorty(method);                         \
        jvalue boxed[64];                                                             \
        if (kudroid::UnpackGuestVaListToJvalues(shorty, reinterpret_cast<const void*>(args), boxed, 64)) { \
            return static_cast<RET_TYPE>(self->CallJavaA(nullptr, method, boxed, false).FIELD); \
        }                                                                              \
        return static_cast<RET_TYPE>(self->CallJavaV(nullptr, method, args, false).FIELD); \
    }                                                                                 \
    RET_TYPE JNICALL CallStatic##NAME##Method(JNIEnv* env, jclass c, jmethodID mid, ...) { \
        va_list args;                                                                 \
        va_start(args, mid);                                                          \
        RET_TYPE r = CallStatic##NAME##MethodV(env, c, mid, args);                    \
        va_end(args);                                                                 \
        return r;                                                                     \
    }

#define DEXRT_DEFINE_ALL_CALLS(NAME, RET_TYPE, FIELD) \
    DEXRT_DEFINE_CALL(NAME, RET_TYPE, FIELD)          \
    DEXRT_DEFINE_CALL_NONVIRTUAL(NAME, RET_TYPE, FIELD) \
    DEXRT_DEFINE_CALL_STATIC(NAME, RET_TYPE, FIELD)

DEXRT_DEFINE_ALL_CALLS(Boolean, jboolean, i)
DEXRT_DEFINE_ALL_CALLS(Byte, jbyte, i)
DEXRT_DEFINE_ALL_CALLS(Char, jchar, i)
DEXRT_DEFINE_ALL_CALLS(Short, jshort, i)
DEXRT_DEFINE_ALL_CALLS(Int, jint, i)
DEXRT_DEFINE_ALL_CALLS(Long, jlong, j)
DEXRT_DEFINE_ALL_CALLS(Float, jfloat, f)
DEXRT_DEFINE_ALL_CALLS(Double, jdouble, d)

#undef DEXRT_DEFINE_ALL_CALLS
#undef DEXRT_DEFINE_CALL_STATIC
#undef DEXRT_DEFINE_CALL_NONVIRTUAL
#undef DEXRT_DEFINE_CALL
#undef DEXRT_CALL_BODY

// The returned object must be a local ref, so general macros cannot be used.
jobject JNICALL CallObjectMethodA(JNIEnv* env, jobject obj, jmethodID mid,
                                 const jvalue* args) {
    DexJniEnv* self = Self(env);
    if (self == nullptr) return nullptr;
    return self->AddLocalRef(self->CallJavaA(Obj(obj), Mth(mid), args, true).l);
}
jobject JNICALL CallObjectMethodV(JNIEnv* env, jobject obj, jmethodID mid, va_list args) {
    DexJniEnv* self = Self(env);
    if (self == nullptr || mid == nullptr) return nullptr;
    DexMethod* method = Mth(mid);
    const char* shorty = DexJniEnv::MethodShorty(method);
    jvalue boxed[64];
    if (kudroid::UnpackGuestVaListToJvalues(shorty, reinterpret_cast<const void*>(args), boxed, 64)) {
        return self->AddLocalRef(self->CallJavaA(Obj(obj), method, boxed, true).l);
    }
    return self->AddLocalRef(self->CallJavaV(Obj(obj), method, args, true).l);
}
jobject JNICALL CallObjectMethod(JNIEnv* env, jobject obj, jmethodID mid, ...) {
    va_list args;
    va_start(args, mid);
    jobject r = CallObjectMethodV(env, obj, mid, args);
    va_end(args);
    return r;
}

jobject JNICALL CallNonvirtualObjectMethodA(JNIEnv* env, jobject obj, jclass, jmethodID mid,
                                           const jvalue* args) {
    DexJniEnv* self = Self(env);
    if (self == nullptr) return nullptr;
    return self->AddLocalRef(self->CallJavaA(Obj(obj), Mth(mid), args, false).l);
}
jobject JNICALL CallNonvirtualObjectMethodV(JNIEnv* env, jobject obj, jclass, jmethodID mid,
                                           va_list args) {
    DexJniEnv* self = Self(env);
    if (self == nullptr || mid == nullptr) return nullptr;
    DexMethod* method = Mth(mid);
    const char* shorty = DexJniEnv::MethodShorty(method);
    jvalue boxed[64];
    if (kudroid::UnpackGuestVaListToJvalues(shorty, reinterpret_cast<const void*>(args), boxed, 64)) {
        return self->AddLocalRef(self->CallJavaA(Obj(obj), method, boxed, false).l);
    }
    return self->AddLocalRef(self->CallJavaV(Obj(obj), method, args, false).l);
}
jobject JNICALL CallNonvirtualObjectMethod(JNIEnv* env, jobject obj, jclass c, jmethodID mid,
                                          ...) {
    va_list args;
    va_start(args, mid);
    jobject r = CallNonvirtualObjectMethodV(env, obj, c, mid, args);
    va_end(args);
    return r;
}

jobject JNICALL CallStaticObjectMethodA(JNIEnv* env, jclass, jmethodID mid,
                                       const jvalue* args) {
    DexJniEnv* self = Self(env);
    if (self == nullptr) return nullptr;
    return self->AddLocalRef(self->CallJavaA(nullptr, Mth(mid), args, false).l);
}
jobject JNICALL CallStaticObjectMethodV(JNIEnv* env, jclass, jmethodID mid, va_list args) {
    DexJniEnv* self = Self(env);
    if (self == nullptr || mid == nullptr) return nullptr;
    DexMethod* method = Mth(mid);
    const char* shorty = DexJniEnv::MethodShorty(method);
    jvalue boxed[64];
    if (kudroid::UnpackGuestVaListToJvalues(shorty, reinterpret_cast<const void*>(args), boxed, 64)) {
        return self->AddLocalRef(self->CallJavaA(nullptr, method, boxed, false).l);
    }
    return self->AddLocalRef(self->CallJavaV(nullptr, method, args, false).l);
}
jobject JNICALL CallStaticObjectMethod(JNIEnv* env, jclass c, jmethodID mid, ...) {
    va_list args;
    va_start(args, mid);
    jobject r = CallStaticObjectMethodV(env, c, mid, args);
    va_end(args);
    return r;
}

// void has no return value so it must be written by hand.
void JNICALL CallVoidMethodA(JNIEnv* env, jobject obj, jmethodID mid, const jvalue* args) {
    if (DexJniEnv* self = Self(env)) self->CallJavaA(Obj(obj), Mth(mid), args, true);
}
void JNICALL CallVoidMethodV(JNIEnv* env, jobject obj, jmethodID mid, va_list args) {
    if (DexJniEnv* self = Self(env)) {
        if (mid == nullptr) return;
        DexMethod* method = Mth(mid);
        const char* shorty = DexJniEnv::MethodShorty(method);
        jvalue boxed[64];
        if (kudroid::UnpackGuestVaListToJvalues(shorty, reinterpret_cast<const void*>(args), boxed, 64)) {
            self->CallJavaA(Obj(obj), method, boxed, true);
        } else {
            self->CallJavaV(Obj(obj), method, args, true);
        }
    }
}
void JNICALL CallVoidMethod(JNIEnv* env, jobject obj, jmethodID mid, ...) {
    va_list args;
    va_start(args, mid);
    CallVoidMethodV(env, obj, mid, args);
    va_end(args);
}

void JNICALL CallNonvirtualVoidMethodA(JNIEnv* env, jobject obj, jclass, jmethodID mid,
                                       const jvalue* args) {
    if (DexJniEnv* self = Self(env)) self->CallJavaA(Obj(obj), Mth(mid), args, false);
}
void JNICALL CallNonvirtualVoidMethodV(JNIEnv* env, jobject obj, jclass, jmethodID mid,
                                       va_list args) {
    if (DexJniEnv* self = Self(env)) {
        if (mid == nullptr) return;
        DexMethod* method = Mth(mid);
        const char* shorty = DexJniEnv::MethodShorty(method);
        jvalue boxed[64];
        if (kudroid::UnpackGuestVaListToJvalues(shorty, reinterpret_cast<const void*>(args), boxed, 64)) {
            self->CallJavaA(Obj(obj), method, boxed, false);
        } else {
            self->CallJavaV(Obj(obj), method, args, false);
        }
    }
}
void JNICALL CallNonvirtualVoidMethod(JNIEnv* env, jobject obj, jclass c, jmethodID mid, ...) {
    va_list args;
    va_start(args, mid);
    CallNonvirtualVoidMethodV(env, obj, c, mid, args);
    va_end(args);
}

void JNICALL CallStaticVoidMethodA(JNIEnv* env, jclass, jmethodID mid, const jvalue* args) {
    if (DexJniEnv* self = Self(env)) self->CallJavaA(nullptr, Mth(mid), args, false);
}
void JNICALL CallStaticVoidMethodV(JNIEnv* env, jclass, jmethodID mid, va_list args) {
    if (DexJniEnv* self = Self(env)) {
        if (mid == nullptr) return;
        DexMethod* method = Mth(mid);
        const char* shorty = DexJniEnv::MethodShorty(method);
        jvalue boxed[64];
        if (kudroid::UnpackGuestVaListToJvalues(shorty, reinterpret_cast<const void*>(args), boxed, 64)) {
            self->CallJavaA(nullptr, method, boxed, false);
        } else {
            self->CallJavaV(nullptr, method, args, false);
        }
    }
}
void JNICALL CallStaticVoidMethod(JNIEnv* env, jclass c, jmethodID mid, ...) {
    va_list args;
    va_start(args, mid);
    CallStaticVoidMethodV(env, c, mid, args);
    va_end(args);
}

// Field instance.

#define DEXRT_DEFINE_FIELD(NAME, RET_TYPE, CTYPE)                                   \
    RET_TYPE JNICALL Get##NAME##Field(JNIEnv*, jobject obj, jfieldID fid) {         \
        DexObject* o = Obj(obj);                                                    \
        DexField* f = Fld(fid);                                                     \
        if (o == nullptr || f == nullptr) return RET_TYPE();                         \
        if (JnibridgeTraceActive()) {                                               \
            std::fprintf(stderr, "[KuART][JNIBRIDGE] Get%sField %s.%s\n", #NAME,     \
                         f->declaring_class != nullptr                              \
                             ? f->declaring_class->PrettyName().c_str()              \
                             : "?",                                                 \
                         f->name != nullptr ? f->name : "?");                       \
        }                                                                            \
        return static_cast<RET_TYPE>(o->GetField<CTYPE>(f->offset_or_slot));         \
    }                                                                                \
    void JNICALL Set##NAME##Field(JNIEnv*, jobject obj, jfieldID fid, RET_TYPE v) { \
        DexObject* o = Obj(obj);                                                    \
        DexField* f = Fld(fid);                                                     \
        if (o == nullptr || f == nullptr) return;                                     \
        o->SetField<CTYPE>(f->offset_or_slot, static_cast<CTYPE>(v));                 \
    }

DEXRT_DEFINE_FIELD(Boolean, jboolean, int8_t)
DEXRT_DEFINE_FIELD(Byte, jbyte, int8_t)
DEXRT_DEFINE_FIELD(Char, jchar, uint16_t)
DEXRT_DEFINE_FIELD(Short, jshort, int16_t)
DEXRT_DEFINE_FIELD(Int, jint, int32_t)
DEXRT_DEFINE_FIELD(Long, jlong, int64_t)
DEXRT_DEFINE_FIELD(Float, jfloat, float)
DEXRT_DEFINE_FIELD(Double, jdouble, double)

#undef DEXRT_DEFINE_FIELD

jobject JNICALL GetObjectField(JNIEnv* env, jobject obj, jfieldID fid) {
    DexJniEnv* self = Self(env);
    DexObject* o = Obj(obj);
    DexField* f = Fld(fid);
    if (self == nullptr || o == nullptr || f == nullptr) return nullptr;
    if (JnibridgeTraceActive()) {
        std::fprintf(stderr, "[KuART][JNIBRIDGE] GetObjectField %s.%s\n",
                     f->declaring_class != nullptr
                         ? f->declaring_class->PrettyName().c_str()
                         : "?",
                     f->name != nullptr ? f->name : "?");
    }
    return self->AddLocalRef(o->GetField<DexObject*>(f->offset_or_slot));
}

void JNICALL SetObjectField(JNIEnv*, jobject obj, jfieldID fid, jobject v) {
    DexObject* o = Obj(obj);
    DexField* f = Fld(fid);
    if (o == nullptr || f == nullptr) return;
    o->SetField<DexObject*>(f->offset_or_slot, Obj(v));
}

// Field static.
// Static value lives in the declaring class, not the passed class.

DexValue* StaticSlot(jfieldID fid) {
    DexField* f = Fld(fid);
    if (f == nullptr || f->declaring_class == nullptr) return nullptr;
    auto& values = f->declaring_class->static_values;
    if (f->offset_or_slot >= values.size()) return nullptr;
    return &values[f->offset_or_slot];
}

#define DEXRT_DEFINE_STATIC_FIELD(NAME, RET_TYPE, FIELD, MAKE)                       \
    RET_TYPE JNICALL GetStatic##NAME##Field(JNIEnv*, jclass, jfieldID fid) {         \
        DexValue* slot = StaticSlot(fid);                                             \
        return slot != nullptr ? static_cast<RET_TYPE>(slot->FIELD) : RET_TYPE();      \
    }                                                                                 \
    void JNICALL SetStatic##NAME##Field(JNIEnv*, jclass, jfieldID fid, RET_TYPE v) { \
        if (DexValue* slot = StaticSlot(fid)) *slot = MAKE;                           \
    }

DEXRT_DEFINE_STATIC_FIELD(Boolean, jboolean, i, DexValue::Int(v != 0 ? 1 : 0))
DEXRT_DEFINE_STATIC_FIELD(Byte, jbyte, i, DexValue::Int(v))
DEXRT_DEFINE_STATIC_FIELD(Char, jchar, i, DexValue::Int(v))
DEXRT_DEFINE_STATIC_FIELD(Short, jshort, i, DexValue::Int(v))
DEXRT_DEFINE_STATIC_FIELD(Int, jint, i, DexValue::Int(v))
DEXRT_DEFINE_STATIC_FIELD(Long, jlong, j, DexValue::Long(v))
DEXRT_DEFINE_STATIC_FIELD(Float, jfloat, f, DexValue::Float(v))
DEXRT_DEFINE_STATIC_FIELD(Double, jdouble, d, DexValue::Double(v))

#undef DEXRT_DEFINE_STATIC_FIELD

jobject JNICALL GetStaticObjectField(JNIEnv* env, jclass, jfieldID fid) {
    DexJniEnv* self = Self(env);
    DexValue* slot = StaticSlot(fid);
    if (self == nullptr || slot == nullptr) return nullptr;
    return self->AddLocalRef(slot->l);
}

void JNICALL SetStaticObjectField(JNIEnv*, jclass, jfieldID fid, jobject v) {
    if (DexValue* slot = StaticSlot(fid)) *slot = DexValue::Ref(Obj(v));
}

// String.

jstring JNICALL NewStringUTF(JNIEnv* env, const char* utf) {
    DexJniEnv* self = Self(env);
    if (self == nullptr || utf == nullptr) return nullptr;
    if (JnibridgeTraceActive()) {
        std::fprintf(stderr, "[KuART][JNIBRIDGE] NewStringUTF \"%.64s\"\n", utf);
    }
    return reinterpret_cast<jstring>(self->AddLocalRef(self->linker()->NewString(utf)));
}

jsize JNICALL GetStringUTFLength(JNIEnv*, jstring str) {
    DexString* s = Str(str);
    return s != nullptr ? static_cast<jsize>(s->length) : 0;
}

const char* JNICALL GetStringUTFChars(JNIEnv*, jstring str, jboolean* isCopy) {
    DexString* s = Str(str);
    if (isCopy != nullptr) *isCopy = JNI_FALSE;  // Return the heap buffer directly
    if (JnibridgeTraceActive()) {
        std::fprintf(stderr, "[KuART][JNIBRIDGE] GetStringUTFChars len=%u \"%.64s\"\n",
                     s != nullptr ? s->length : 0,
                     (s != nullptr && s->utf8 != nullptr) ? s->utf8 : "(null)");
    }
    return s != nullptr ? s->utf8 : nullptr;
}

void JNICALL ReleaseStringUTFChars(JNIEnv*, jstring, const char*) {
    // GetStringUTFChars doesn't copy so there's nothing to free.
}

// KuART keeps UTF-8; UTF-16 functions convert temporarily when required by native. Processing only
// ASCII because the framework stub and the app's class/method names are all ASCII.
jsize JNICALL GetStringLength(JNIEnv*, jstring str) {
    DexString* s = Str(str);
    return s != nullptr ? static_cast<jsize>(s->length) : 0;
}

const jchar* JNICALL GetStringChars(JNIEnv*, jstring str, jboolean* isCopy) {
    DexString* s = Str(str);
    if (s == nullptr) return nullptr;
    auto* buf = static_cast<jchar*>(std::malloc((s->length + 1) * sizeof(jchar)));
    if (buf == nullptr) return nullptr;
    for (uint32_t i = 0; i < s->length; ++i) {
        buf[i] = static_cast<jchar>(static_cast<uint8_t>(s->utf8[i]));
    }
    buf[s->length] = 0;
    if (isCopy != nullptr) *isCopy = JNI_TRUE;
    return buf;
}

void JNICALL ReleaseStringChars(JNIEnv*, jstring, const jchar* chars) {
    std::free(const_cast<jchar*>(chars));
}

jstring JNICALL NewString(JNIEnv* env, const jchar* unicode, jsize len) {
    DexJniEnv* self = Self(env);
    if (self == nullptr || unicode == nullptr || len < 0) return nullptr;
    std::string utf8;
    utf8.reserve(static_cast<size_t>(len));
    for (jsize i = 0; i < len; ++i) utf8 += static_cast<char>(unicode[i] & 0x7F);
    return reinterpret_cast<jstring>(self->AddLocalRef(self->linker()->NewString(utf8.c_str())));
}

void JNICALL GetStringRegion(JNIEnv*, jstring str, jsize start, jsize len, jchar* buf) {
    DexString* s = Str(str);
    if (s == nullptr || buf == nullptr || start < 0 || len < 0) return;
    if (static_cast<uint32_t>(start + len) > s->length) return;
    for (jsize i = 0; i < len; ++i) {
        buf[i] = static_cast<jchar>(static_cast<uint8_t>(s->utf8[start + i]));
    }
}

void JNICALL GetStringUTFRegion(JNIEnv*, jstring str, jsize start, jsize len, char* buf) {
    DexString* s = Str(str);
    if (s == nullptr || buf == nullptr || start < 0 || len < 0) return;
    if (static_cast<uint32_t>(start + len) > s->length) return;
    std::memcpy(buf, s->utf8 + start, static_cast<size_t>(len));
    buf[len] = '\0';
}

const jchar* JNICALL GetStringCritical(JNIEnv* env, jstring str, jboolean* isCopy) {
    return GetStringChars(env, str, isCopy);
}

void JNICALL ReleaseStringCritical(JNIEnv* env, jstring str, const jchar* chars) {
    ReleaseStringChars(env, str, chars);
}

jlong JNICALL GetStringUTFLengthAsLong(JNIEnv*, jstring str) {
    DexString* s = Str(str);
    return s != nullptr ? static_cast<jlong>(s->length) : 0;
}

// Array.

jsize JNICALL GetArrayLength(JNIEnv*, jarray array) {
    DexArray* a = Arr(array);
    const jsize len = a != nullptr ? a->length : 0;
    if (JnibridgeTraceActive()) {
        std::fprintf(stderr, "[KuART][JNIBRIDGE] GetArrayLength -> %d\n", len);
    }
    return len;
}

// Primitive array: array class name is descriptor with '[' in front.
jarray NewPrimitiveArray(JNIEnv* env, const char* array_descriptor, jsize len) {
    DexJniEnv* self = Self(env);
    if (self == nullptr || len < 0) return nullptr;
    DexClass* klass = self->linker()->FindClass(array_descriptor);
    if (klass == nullptr) return nullptr;
    return reinterpret_cast<jarray>(self->AddLocalRef(self->linker()->AllocArray(klass, len)));
}

jbooleanArray JNICALL NewBooleanArray(JNIEnv* env, jsize len) {
    return reinterpret_cast<jbooleanArray>(NewPrimitiveArray(env, "[Z", len));
}
jbyteArray JNICALL NewByteArray(JNIEnv* env, jsize len) {
    return reinterpret_cast<jbyteArray>(NewPrimitiveArray(env, "[B", len));
}
jcharArray JNICALL NewCharArray(JNIEnv* env, jsize len) {
    return reinterpret_cast<jcharArray>(NewPrimitiveArray(env, "[C", len));
}
jshortArray JNICALL NewShortArray(JNIEnv* env, jsize len) {
    return reinterpret_cast<jshortArray>(NewPrimitiveArray(env, "[S", len));
}
jintArray JNICALL NewIntArray(JNIEnv* env, jsize len) {
    return reinterpret_cast<jintArray>(NewPrimitiveArray(env, "[I", len));
}
jlongArray JNICALL NewLongArray(JNIEnv* env, jsize len) {
    return reinterpret_cast<jlongArray>(NewPrimitiveArray(env, "[J", len));
}
jfloatArray JNICALL NewFloatArray(JNIEnv* env, jsize len) {
    return reinterpret_cast<jfloatArray>(NewPrimitiveArray(env, "[F", len));
}
jdoubleArray JNICALL NewDoubleArray(JNIEnv* env, jsize len) {
    return reinterpret_cast<jdoubleArray>(NewPrimitiveArray(env, "[D", len));
}

jobjectArray JNICALL NewObjectArray(JNIEnv* env, jsize len, jclass clazz, jobject init) {
    DexJniEnv* self = Self(env);
    if (self == nullptr || len < 0) return nullptr;
    DexClass* component = CheckedCls(self, clazz);
    if (component == nullptr) return nullptr;

    std::string array_descriptor = "[";
    array_descriptor += component->descriptor;
    DexClass* array_class = self->linker()->FindClass(array_descriptor.c_str());
    if (array_class == nullptr) return nullptr;

    DexArray* arr = self->linker()->AllocArray(array_class, len);
    if (arr == nullptr) return nullptr;
    if (init != nullptr) {
        for (jsize i = 0; i < len; ++i) arr->Set<DexObject*>(i, Obj(init));
    }
    return reinterpret_cast<jobjectArray>(self->AddLocalRef(arr));
}

jobject JNICALL GetObjectArrayElement(JNIEnv* env, jobjectArray array, jsize index) {
    DexJniEnv* self = Self(env);
    DexArray* a = Arr(array);
    if (self == nullptr || a == nullptr || index < 0 || index >= a->length) return nullptr;
    DexObject* elem = a->Get<DexObject*>(index);
    if (JnibridgeTraceActive()) {
        DexClass* ec =
            (elem != nullptr && self->linker() != nullptr)
                ? self->linker()->ClassOfObject(elem)
                : nullptr;
        std::fprintf(stderr, "[KuART][JNIBRIDGE] GetObjectArrayElement [%d] -> %s\n",
                     index, ec != nullptr ? ec->PrettyName().c_str() : "(null)");
    }
    return self->AddLocalRef(elem);
}

void JNICALL SetObjectArrayElement(JNIEnv*, jobjectArray array, jsize index, jobject val) {
    DexArray* a = Arr(array);
    if (a == nullptr || index < 0 || index >= a->length) return;
    a->Set<DexObject*>(index, Obj(val));
}

// Get<Type>ArrayElements returns a STRAIGHT pointer to the array (no copy) - valid because
// heap does not move objects. Release is therefore a no-op.
#define DEXRT_DEFINE_ARRAY(NAME, ELEM_TYPE, ARRAY_TYPE)                              \
    ELEM_TYPE* JNICALL Get##NAME##ArrayElements(JNIEnv*, ARRAY_TYPE array,           \
                                               jboolean* isCopy) {                   \
        DexArray* a = Arr(array);                                                     \
        if (a == nullptr) return nullptr;                                             \
        if (isCopy != nullptr) *isCopy = JNI_FALSE;                                   \
        return reinterpret_cast<ELEM_TYPE*>(a->Data());                               \
    }                                                                                 \
    void JNICALL Release##NAME##ArrayElements(JNIEnv*, ARRAY_TYPE, ELEM_TYPE*, jint) {} \
    void JNICALL Get##NAME##ArrayRegion(JNIEnv*, ARRAY_TYPE array, jsize start,       \
                                       jsize len, ELEM_TYPE* buf) {                   \
        DexArray* a = Arr(array);                                                     \
        if (a == nullptr || buf == nullptr || start < 0 || len < 0) return;            \
        if (start + len > a->length) return;                                          \
        std::memcpy(buf, a->Data() + static_cast<size_t>(start) * sizeof(ELEM_TYPE),  \
                    static_cast<size_t>(len) * sizeof(ELEM_TYPE));                    \
    }                                                                                 \
    void JNICALL Set##NAME##ArrayRegion(JNIEnv*, ARRAY_TYPE array, jsize start,       \
                                       jsize len, const ELEM_TYPE* buf) {             \
        DexArray* a = Arr(array);                                                     \
        if (a == nullptr || buf == nullptr || start < 0 || len < 0) return;            \
        if (start + len > a->length) return;                                          \
        std::memcpy(a->Data() + static_cast<size_t>(start) * sizeof(ELEM_TYPE), buf,  \
                    static_cast<size_t>(len) * sizeof(ELEM_TYPE));                    \
    }

DEXRT_DEFINE_ARRAY(Boolean, jboolean, jbooleanArray)
DEXRT_DEFINE_ARRAY(Byte, jbyte, jbyteArray)
DEXRT_DEFINE_ARRAY(Char, jchar, jcharArray)
DEXRT_DEFINE_ARRAY(Short, jshort, jshortArray)
DEXRT_DEFINE_ARRAY(Int, jint, jintArray)
DEXRT_DEFINE_ARRAY(Long, jlong, jlongArray)
DEXRT_DEFINE_ARRAY(Float, jfloat, jfloatArray)
DEXRT_DEFINE_ARRAY(Double, jdouble, jdoubleArray)

#undef DEXRT_DEFINE_ARRAY

void* JNICALL GetPrimitiveArrayCritical(JNIEnv*, jarray array, jboolean* isCopy) {
    DexArray* a = Arr(array);
    if (a == nullptr) return nullptr;
    if (isCopy != nullptr) *isCopy = JNI_FALSE;
    return a->Data();
}

void JNICALL ReleasePrimitiveArrayCritical(JNIEnv*, jarray, void*, jint) {}

// Native method, monitor, VM.

jint JNICALL RegisterNatives(JNIEnv* env, jclass clazz, const JNINativeMethod* methods,
                            jint nMethods) {
    DexJniEnv* self = Self(env);
    if (self == nullptr) return JNI_ERR;
    // A bad handle here would walk the method lists of whatever the pointer
    // happens to address, and RegisterNatives is called from JNI_OnLoad before
    // anything else has run - the worst place to corrupt state.
    DexClass* k = CheckedCls(self, clazz);
    if (k == nullptr) {
        ThrowLookupFailure(self, "Ljava/lang/NoClassDefFoundError;",
                           "RegisterNatives with an invalid class handle");
        return JNI_ERR;
    }
    return self->RegisterNatives(k, methods, nMethods);
}

jint JNICALL UnregisterNatives(JNIEnv* env, jclass clazz) {
    DexClass* k = CheckedCls(Self(env), clazz);
    if (k == nullptr) return JNI_ERR;
    for (DexMethod& m : k->direct_methods) m.native_fn = nullptr;
    for (DexMethod& m : k->virtual_methods) m.native_fn = nullptr;
    return JNI_OK;
}

jint JNICALL MonitorEnter(JNIEnv*, jobject obj) {
    DexObject* o = Obj(obj);
    if (o == nullptr) return JNI_ERR;
    ++o->lock_count;
    return JNI_OK;
}

jint JNICALL MonitorExit(JNIEnv*, jobject obj) {
    DexObject* o = Obj(obj);
    if (o == nullptr) return JNI_ERR;
    if (o->lock_count > 0) --o->lock_count;
    return JNI_OK;
}

jint JNICALL GetJavaVM(JNIEnv* env, JavaVM** vm) {
    DexJniEnv* self = Self(env);
    if (self == nullptr || vm == nullptr) return JNI_ERR;
    *vm = self->vm();
    return JNI_OK;
}

jobject JNICALL NewDirectByteBuffer(JNIEnv* env, void* address, jlong capacity) {
    DexJniEnv* self = Self(env);
    if (self == nullptr || self->linker() == nullptr) return nullptr;
    DexClass* dbb_class = self->linker()->FindClass("Ljava/nio/DirectByteBuffer;");
    if (dbb_class == nullptr) dbb_class = self->linker()->FindClass("Ljava/nio/ByteBuffer;");
    if (dbb_class == nullptr) return nullptr;

    DexObject* obj = self->linker()->AllocObject(dbb_class);
    if (obj == nullptr) return nullptr;

    const DexField* f_addr = dbb_class->FindInstanceField("address", "J");
    if (f_addr != nullptr) {
        obj->SetField<int64_t>(f_addr->offset_or_slot, reinterpret_cast<int64_t>(address));
    }
    const DexField* f_cap = dbb_class->FindInstanceField("capacity", "I");
    if (f_cap != nullptr) {
        obj->SetField<int32_t>(f_cap->offset_or_slot, static_cast<int32_t>(capacity));
    }
    const DexField* f_lim = dbb_class->FindInstanceField("limit", "I");
    if (f_lim != nullptr) {
        obj->SetField<int32_t>(f_lim->offset_or_slot, static_cast<int32_t>(capacity));
    }

    return self->AddLocalRef(obj);
}

void* JNICALL GetDirectBufferAddress(JNIEnv*, jobject buf) {
    DexObject* obj = Obj(buf);
    if (obj == nullptr || obj->clazz == nullptr) return nullptr;

    const DexField* f_addr = obj->clazz->FindInstanceField("address", "J");
    if (f_addr != nullptr) {
        int64_t addr = obj->GetField<int64_t>(f_addr->offset_or_slot);
        // Diagnostic: native writers mix through this pointer; 0 means crash.
        static std::atomic<int> s_logged{0};
        const int n = s_logged.load();
        if (n < 8 || (addr == 0 && n < 20)) {
            ++s_logged;
            int32_t cap = -1;
            if (const DexField* f_cap = obj->clazz->FindInstanceField("capacity", "I")) {
                cap = obj->GetField<int32_t>(f_cap->offset_or_slot);
            }
            std::fprintf(stderr, "[KuDroidBuf] GetDirectBufferAddress addr=%p cap=%d%s\n",
                         reinterpret_cast<void*>(static_cast<uintptr_t>(addr)), cap,
                         addr == 0 ? " NULL-DEST" : "");
        }
        return reinterpret_cast<void*>(addr);
    }
    return nullptr;
}

jlong JNICALL GetDirectBufferCapacity(JNIEnv*, jobject buf) {
    DexObject* obj = Obj(buf);
    if (obj == nullptr || obj->clazz == nullptr) return -1;

    const DexField* f_cap = obj->clazz->FindInstanceField("capacity", "I");
    if (f_cap != nullptr) {
        return obj->GetField<int32_t>(f_cap->offset_or_slot);
    }
    return -1;
}

jmethodID JNICALL FromReflectedMethod(JNIEnv*, jobject method) {
    DexObject* obj = Obj(method);
    if (obj == nullptr || obj->clazz == nullptr) {
        std::fprintf(stderr, "[KuART][JNI] FromReflectedMethod with null/invalid object\n");
        return nullptr;
    }
    const DexField* f_handle = obj->clazz->FindInstanceField("artMethod", "J");
    if (f_handle != nullptr) {
        int64_t m = obj->GetField<int64_t>(f_handle->offset_or_slot);
        if (m == 0) {
            std::fprintf(stderr, "[KuART][JNI] FromReflectedMethod with zero artMethod\n");
        } else if (JnibridgeTraceActive()) {
            const DexMethod* dm = reinterpret_cast<const DexMethod*>(
                static_cast<uintptr_t>(m));
            std::fprintf(stderr, "[KuART][JNIBRIDGE] FromReflectedMethod -> %s.%s%s\n",
                         dm->declaring_class != nullptr
                             ? dm->declaring_class->PrettyName().c_str()
                             : "?",
                         dm->name != nullptr ? dm->name : "?",
                         dm->signature != nullptr ? dm->signature : "?");
        }
        return reinterpret_cast<jmethodID>(m);
    }
    std::fprintf(stderr, "[KuART][JNI] FromReflectedMethod with no artMethod field\n");
    return nullptr;
}

jfieldID JNICALL FromReflectedField(JNIEnv*, jobject field) {
    DexObject* obj = Obj(field);
    if (obj == nullptr || obj->clazz == nullptr) return nullptr;
    const DexField* f_handle = obj->clazz->FindInstanceField("artField", "J");
    if (f_handle != nullptr) {
        int64_t f = obj->GetField<int64_t>(f_handle->offset_or_slot);
        if (JnibridgeTraceActive()) {
            const DexField* df =
                reinterpret_cast<const DexField*>(static_cast<uintptr_t>(f));
            std::fprintf(stderr, "[KuART][JNIBRIDGE] FromReflectedField -> %s.%s\n",
                         (df != nullptr && df->declaring_class != nullptr)
                             ? df->declaring_class->PrettyName().c_str()
                             : "?",
                         (df != nullptr && df->name != nullptr) ? df->name : "?");
        }
        return reinterpret_cast<jfieldID>(f);
    }
    return nullptr;
}

jobject JNICALL ToReflectedMethod(JNIEnv* env, jclass clazz, jmethodID methodID, jboolean /*isStatic*/) {
    DexJniEnv* self = Self(env);
    if (self == nullptr || methodID == nullptr) return nullptr;
    DexMethod* m = reinterpret_cast<DexMethod*>(methodID);
    DexClass* method_cls = self->linker()->FindClass("Ljava/lang/reflect/Method;");
    if (method_cls == nullptr) return nullptr;
    DexObject* obj = self->linker()->AllocObject(method_cls);
    if (obj == nullptr) return nullptr;
    const DexField* f_handle = method_cls->FindInstanceField("artMethod", "J");
    if (f_handle != nullptr) {
        obj->SetField<int64_t>(f_handle->offset_or_slot, reinterpret_cast<int64_t>(methodID));
    }
    // Fill declaringClass/name; null traps later reflective uses.
    DexClass* declaring = (m->declaring_class != nullptr) ? m->declaring_class
                         : CheckedCls(self, clazz);
    const DexField* f_decl = method_cls->FindInstanceField("declaringClass", "Ljava/lang/Class;");
    if (f_decl != nullptr && declaring != nullptr) {
        obj->SetField<DexObject*>(f_decl->offset_or_slot,
                                  self->linker()->GetClassObject(declaring));
    }
    const DexField* f_name = method_cls->FindInstanceField("name", "Ljava/lang/String;");
    if (f_name != nullptr && m->name != nullptr) {
        obj->SetField<DexObject*>(f_name->offset_or_slot, self->linker()->NewString(m->name));
    }
    return self->AddLocalRef(obj);
}

jobject JNICALL ToReflectedField(JNIEnv* env, jclass clazz, jfieldID fieldID, jboolean /*isStatic*/) {
    DexJniEnv* self = Self(env);
    if (self == nullptr || fieldID == nullptr) return nullptr;
    DexField* f = reinterpret_cast<DexField*>(fieldID);
    DexClass* field_cls = self->linker()->FindClass("Ljava/lang/reflect/Field;");
    if (field_cls == nullptr) return nullptr;
    DexObject* obj = self->linker()->AllocObject(field_cls);
    if (obj == nullptr) return nullptr;
    const DexField* f_handle = field_cls->FindInstanceField("artField", "J");
    if (f_handle != nullptr) {
        obj->SetField<int64_t>(f_handle->offset_or_slot, reinterpret_cast<int64_t>(fieldID));
    }
    // Same completeness rule as ToReflectedMethod above: a Field with null
    // declaringClass/name poisons every later reflective use of it.
    DexClass* declaring = (f->declaring_class != nullptr) ? f->declaring_class
                         : CheckedCls(self, clazz);
    const DexField* f_decl = field_cls->FindInstanceField("declaringClass", "Ljava/lang/Class;");
    if (f_decl != nullptr && declaring != nullptr) {
        obj->SetField<DexObject*>(f_decl->offset_or_slot,
                                  self->linker()->GetClassObject(declaring));
    }
    const DexField* f_name = field_cls->FindInstanceField("name", "Ljava/lang/String;");
    if (f_name != nullptr && f->name != nullptr) {
        obj->SetField<DexObject*>(f_name->offset_or_slot, self->linker()->NewString(f->name));
    }
    return self->AddLocalRef(obj);
}

jobject JNICALL GetModule(JNIEnv*, jclass) { return nullptr; }
jboolean JNICALL IsVirtualThread(JNIEnv*, jobject) { return JNI_FALSE; }
jboolean JNICALL HasIdentity(JNIEnv*, jobject) { return JNI_TRUE; }

// JavaVM.

jint JNICALL DestroyJavaVM(JavaVM*) { return JNI_OK; }

jint JNICALL AttachCurrentThread(JavaVM* vm, void** penv, void*) {
    DexJniEnv* self = DexJniEnv::FromVm(vm);
    if (self == nullptr || penv == nullptr) return JNI_ERR;
    *penv = self->env();
    return JNI_OK;
}

jint JNICALL DetachCurrentThread(JavaVM*) { return JNI_OK; }

jint JNICALL GetEnv(JavaVM* vm, void** penv, jint) {
    DexJniEnv* self = DexJniEnv::FromVm(vm);
    if (self == nullptr || penv == nullptr) return JNI_ERR;
    *penv = self->env();
    return JNI_OK;
}

jint JNICALL AttachCurrentThreadAsDaemon(JavaVM* vm, void** penv, void* args) {
    return AttachCurrentThread(vm, penv, args);
}

}  // namespace jnifns
}  // namespace

#if defined(__aarch64__)
extern "C" {
void* kudroid_jni_call_virtual_trampoline(void);
void* kudroid_jni_call_virtual_float_trampoline(void);
void* kudroid_jni_call_virtual_double_trampoline(void);
void* kudroid_jni_call_virtual_object_trampoline(void);

void* kudroid_jni_call_static_trampoline(void);
void* kudroid_jni_call_static_float_trampoline(void);
void* kudroid_jni_call_static_double_trampoline(void);
void* kudroid_jni_call_static_object_trampoline(void);

void* kudroid_jni_call_nonvirtual_trampoline(void);
void* kudroid_jni_call_nonvirtual_float_trampoline(void);
void* kudroid_jni_call_nonvirtual_double_trampoline(void);
void* kudroid_jni_call_nonvirtual_object_trampoline(void);

void* kudroid_jni_new_object_trampoline(void);

uint64_t kudroid_jni_call_virtual_from_registers(const uint64_t* frame) {
    const auto* regs = reinterpret_cast<const kudroid::GuestVarargs*>(frame);
    JNIEnv* env = reinterpret_cast<JNIEnv*>(regs->gp[0]);
    jobject obj = reinterpret_cast<jobject>(regs->gp[1]);
    jmethodID mid = reinterpret_cast<jmethodID>(regs->gp[2]);
    DexJniEnv* self = jnifns::Self(env);
    if (!self || !mid) return 0;
    DexMethod* method = jnifns::Mth(mid);
    const char* shorty = DexJniEnv::MethodShorty(method);
    jvalue args[64];
    kudroid::UnpackGuestVarargsToJvalues(shorty, regs, 3, 0, args, 64);
    return self->CallJavaA(jnifns::Obj(obj), method, args, true).j;
}

float kudroid_jni_call_virtual_float_from_registers(const uint64_t* frame) {
    const auto* regs = reinterpret_cast<const kudroid::GuestVarargs*>(frame);
    JNIEnv* env = reinterpret_cast<JNIEnv*>(regs->gp[0]);
    jobject obj = reinterpret_cast<jobject>(regs->gp[1]);
    jmethodID mid = reinterpret_cast<jmethodID>(regs->gp[2]);
    DexJniEnv* self = jnifns::Self(env);
    if (!self || !mid) return 0.0f;
    DexMethod* method = jnifns::Mth(mid);
    const char* shorty = DexJniEnv::MethodShorty(method);
    jvalue args[64];
    kudroid::UnpackGuestVarargsToJvalues(shorty, regs, 3, 0, args, 64);
    return self->CallJavaA(jnifns::Obj(obj), method, args, true).f;
}

double kudroid_jni_call_virtual_double_from_registers(const uint64_t* frame) {
    const auto* regs = reinterpret_cast<const kudroid::GuestVarargs*>(frame);
    JNIEnv* env = reinterpret_cast<JNIEnv*>(regs->gp[0]);
    jobject obj = reinterpret_cast<jobject>(regs->gp[1]);
    jmethodID mid = reinterpret_cast<jmethodID>(regs->gp[2]);
    DexJniEnv* self = jnifns::Self(env);
    if (!self || !mid) return 0.0;
    DexMethod* method = jnifns::Mth(mid);
    const char* shorty = DexJniEnv::MethodShorty(method);
    jvalue args[64];
    kudroid::UnpackGuestVarargsToJvalues(shorty, regs, 3, 0, args, 64);
    return self->CallJavaA(jnifns::Obj(obj), method, args, true).d;
}

jobject kudroid_jni_call_virtual_object_from_registers(const uint64_t* frame) {
    const auto* regs = reinterpret_cast<const kudroid::GuestVarargs*>(frame);
    JNIEnv* env = reinterpret_cast<JNIEnv*>(regs->gp[0]);
    jobject obj = reinterpret_cast<jobject>(regs->gp[1]);
    jmethodID mid = reinterpret_cast<jmethodID>(regs->gp[2]);
    DexJniEnv* self = jnifns::Self(env);
    if (!self || !mid) return nullptr;
    DexMethod* method = jnifns::Mth(mid);
    const char* shorty = DexJniEnv::MethodShorty(method);
    jvalue args[64];
    kudroid::UnpackGuestVarargsToJvalues(shorty, regs, 3, 0, args, 64);
    return self->AddLocalRef(self->CallJavaA(jnifns::Obj(obj), method, args, true).l);
}

uint64_t kudroid_jni_call_static_from_registers(const uint64_t* frame) {
    const auto* regs = reinterpret_cast<const kudroid::GuestVarargs*>(frame);
    JNIEnv* env = reinterpret_cast<JNIEnv*>(regs->gp[0]);
    jmethodID mid = reinterpret_cast<jmethodID>(regs->gp[2]);
    DexJniEnv* self = jnifns::Self(env);
    if (!self || !mid) return 0;
    DexMethod* method = jnifns::Mth(mid);
    const char* shorty = DexJniEnv::MethodShorty(method);
    jvalue args[64];
    kudroid::UnpackGuestVarargsToJvalues(shorty, regs, 3, 0, args, 64);
    return self->CallJavaA(nullptr, method, args, false).j;
}

float kudroid_jni_call_static_float_from_registers(const uint64_t* frame) {
    const auto* regs = reinterpret_cast<const kudroid::GuestVarargs*>(frame);
    JNIEnv* env = reinterpret_cast<JNIEnv*>(regs->gp[0]);
    jmethodID mid = reinterpret_cast<jmethodID>(regs->gp[2]);
    DexJniEnv* self = jnifns::Self(env);
    if (!self || !mid) return 0.0f;
    DexMethod* method = jnifns::Mth(mid);
    const char* shorty = DexJniEnv::MethodShorty(method);
    jvalue args[64];
    kudroid::UnpackGuestVarargsToJvalues(shorty, regs, 3, 0, args, 64);
    return self->CallJavaA(nullptr, method, args, false).f;
}

double kudroid_jni_call_static_double_from_registers(const uint64_t* frame) {
    const auto* regs = reinterpret_cast<const kudroid::GuestVarargs*>(frame);
    JNIEnv* env = reinterpret_cast<JNIEnv*>(regs->gp[0]);
    jmethodID mid = reinterpret_cast<jmethodID>(regs->gp[2]);
    DexJniEnv* self = jnifns::Self(env);
    if (!self || !mid) return 0.0;
    DexMethod* method = jnifns::Mth(mid);
    const char* shorty = DexJniEnv::MethodShorty(method);
    jvalue args[64];
    kudroid::UnpackGuestVarargsToJvalues(shorty, regs, 3, 0, args, 64);
    return self->CallJavaA(nullptr, method, args, false).d;
}

jobject kudroid_jni_call_static_object_from_registers(const uint64_t* frame) {
    const auto* regs = reinterpret_cast<const kudroid::GuestVarargs*>(frame);
    JNIEnv* env = reinterpret_cast<JNIEnv*>(regs->gp[0]);
    jmethodID mid = reinterpret_cast<jmethodID>(regs->gp[2]);
    DexJniEnv* self = jnifns::Self(env);
    if (!self || !mid) return nullptr;
    DexMethod* method = jnifns::Mth(mid);
    const char* shorty = DexJniEnv::MethodShorty(method);
    jvalue args[64];
    kudroid::UnpackGuestVarargsToJvalues(shorty, regs, 3, 0, args, 64);
    return self->AddLocalRef(self->CallJavaA(nullptr, method, args, false).l);
}

uint64_t kudroid_jni_call_nonvirtual_from_registers(const uint64_t* frame) {
    const auto* regs = reinterpret_cast<const kudroid::GuestVarargs*>(frame);
    JNIEnv* env = reinterpret_cast<JNIEnv*>(regs->gp[0]);
    jobject obj = reinterpret_cast<jobject>(regs->gp[1]);
    jmethodID mid = reinterpret_cast<jmethodID>(regs->gp[3]);
    DexJniEnv* self = jnifns::Self(env);
    if (!self || !mid) return 0;
    DexMethod* method = jnifns::Mth(mid);
    const char* shorty = DexJniEnv::MethodShorty(method);
    jvalue args[64];
    kudroid::UnpackGuestVarargsToJvalues(shorty, regs, 4, 0, args, 64);
    return self->CallJavaA(jnifns::Obj(obj), method, args, false).j;
}

float kudroid_jni_call_nonvirtual_float_from_registers(const uint64_t* frame) {
    const auto* regs = reinterpret_cast<const kudroid::GuestVarargs*>(frame);
    JNIEnv* env = reinterpret_cast<JNIEnv*>(regs->gp[0]);
    jobject obj = reinterpret_cast<jobject>(regs->gp[1]);
    jmethodID mid = reinterpret_cast<jmethodID>(regs->gp[3]);
    DexJniEnv* self = jnifns::Self(env);
    if (!self || !mid) return 0.0f;
    DexMethod* method = jnifns::Mth(mid);
    const char* shorty = DexJniEnv::MethodShorty(method);
    jvalue args[64];
    kudroid::UnpackGuestVarargsToJvalues(shorty, regs, 4, 0, args, 64);
    return self->CallJavaA(jnifns::Obj(obj), method, args, false).f;
}

double kudroid_jni_call_nonvirtual_double_from_registers(const uint64_t* frame) {
    const auto* regs = reinterpret_cast<const kudroid::GuestVarargs*>(frame);
    JNIEnv* env = reinterpret_cast<JNIEnv*>(regs->gp[0]);
    jobject obj = reinterpret_cast<jobject>(regs->gp[1]);
    jmethodID mid = reinterpret_cast<jmethodID>(regs->gp[3]);
    DexJniEnv* self = jnifns::Self(env);
    if (!self || !mid) return 0.0;
    DexMethod* method = jnifns::Mth(mid);
    const char* shorty = DexJniEnv::MethodShorty(method);
    jvalue args[64];
    kudroid::UnpackGuestVarargsToJvalues(shorty, regs, 4, 0, args, 64);
    return self->CallJavaA(jnifns::Obj(obj), method, args, false).d;
}

jobject kudroid_jni_call_nonvirtual_object_from_registers(const uint64_t* frame) {
    const auto* regs = reinterpret_cast<const kudroid::GuestVarargs*>(frame);
    JNIEnv* env = reinterpret_cast<JNIEnv*>(regs->gp[0]);
    jobject obj = reinterpret_cast<jobject>(regs->gp[1]);
    jmethodID mid = reinterpret_cast<jmethodID>(regs->gp[3]);
    DexJniEnv* self = jnifns::Self(env);
    if (!self || !mid) return nullptr;
    DexMethod* method = jnifns::Mth(mid);
    const char* shorty = DexJniEnv::MethodShorty(method);
    jvalue args[64];
    kudroid::UnpackGuestVarargsToJvalues(shorty, regs, 4, 0, args, 64);
    return self->AddLocalRef(self->CallJavaA(jnifns::Obj(obj), method, args, false).l);
}

jobject kudroid_jni_new_object_from_registers(const uint64_t* frame) {
    const auto* regs = reinterpret_cast<const kudroid::GuestVarargs*>(frame);
    JNIEnv* env = reinterpret_cast<JNIEnv*>(regs->gp[0]);
    jclass clazz = reinterpret_cast<jclass>(regs->gp[1]);
    jmethodID mid = reinterpret_cast<jmethodID>(regs->gp[2]);
    DexJniEnv* self = jnifns::Self(env);
    if (!self || !clazz || !mid) return nullptr;
    DexClass* k = jnifns::CheckedCls(self, clazz);
    if (k == nullptr) return nullptr;
    DexMethod* method = jnifns::Mth(mid);
    const char* shorty = DexJniEnv::MethodShorty(method);
    jvalue args[64];
    kudroid::UnpackGuestVarargsToJvalues(shorty, regs, 3, 0, args, 64);
    return self->AddLocalRef(self->NewObjectA(k, method, args));
}
}
#endif

void DexJniEnv::InitFunctionTable() {
    // static: table shared by all DexJniEnv, initialized only once.
    static JNINativeInterface_ fns = {};
    static JNIInvokeInterface_ vm_fns = {};
    static bool initialized = false;

    if (!initialized) {
        fns.GetVersion = jnifns::GetVersion;
        fns.DefineClass = jnifns::DefineClass;
        fns.FindClass = jnifns::FindClass;
        fns.FromReflectedMethod = jnifns::FromReflectedMethod;
        fns.FromReflectedField = jnifns::FromReflectedField;
        fns.ToReflectedMethod = jnifns::ToReflectedMethod;
        fns.GetSuperclass = jnifns::GetSuperclass;
        fns.IsAssignableFrom = jnifns::IsAssignableFrom;
        fns.ToReflectedField = jnifns::ToReflectedField;
        fns.Throw = jnifns::Throw;
        fns.ThrowNew = jnifns::ThrowNew;
        fns.ExceptionOccurred = jnifns::ExceptionOccurred;
        fns.ExceptionDescribe = jnifns::ExceptionDescribe;
        fns.ExceptionClear = jnifns::ExceptionClear;
        fns.FatalError = jnifns::FatalError;
        fns.PushLocalFrame = jnifns::PushLocalFrame;
        fns.PopLocalFrame = jnifns::PopLocalFrame;
        fns.NewGlobalRef = jnifns::NewGlobalRef;
        fns.DeleteGlobalRef = jnifns::DeleteGlobalRef;
        fns.DeleteLocalRef = jnifns::DeleteLocalRef;
        fns.IsSameObject = jnifns::IsSameObject;
        fns.NewLocalRef = jnifns::NewLocalRef;
        fns.EnsureLocalCapacity = jnifns::EnsureLocalCapacity;
        fns.AllocObject = jnifns::AllocObject;
        fns.NewObject = jnifns::NewObject;
        fns.NewObjectV = jnifns::NewObjectV;
        fns.NewObjectA = jnifns::NewObjectA;
        fns.GetObjectClass = jnifns::GetObjectClass;
        fns.IsInstanceOf = jnifns::IsInstanceOf;
        fns.GetMethodID = jnifns::GetMethodID;

#define DEXRT_BIND_CALLS(NAME)                                                      \
        fns.Call##NAME##Method = jnifns::Call##NAME##Method;                        \
        fns.Call##NAME##MethodV = jnifns::Call##NAME##MethodV;                      \
        fns.Call##NAME##MethodA = jnifns::Call##NAME##MethodA;                      \
        fns.CallNonvirtual##NAME##Method = jnifns::CallNonvirtual##NAME##Method;     \
        fns.CallNonvirtual##NAME##MethodV = jnifns::CallNonvirtual##NAME##MethodV;   \
        fns.CallNonvirtual##NAME##MethodA = jnifns::CallNonvirtual##NAME##MethodA;   \
        fns.CallStatic##NAME##Method = jnifns::CallStatic##NAME##Method;             \
        fns.CallStatic##NAME##MethodV = jnifns::CallStatic##NAME##MethodV;           \
        fns.CallStatic##NAME##MethodA = jnifns::CallStatic##NAME##MethodA;

        DEXRT_BIND_CALLS(Object)
        DEXRT_BIND_CALLS(Boolean)
        DEXRT_BIND_CALLS(Byte)
        DEXRT_BIND_CALLS(Char)
        DEXRT_BIND_CALLS(Short)
        DEXRT_BIND_CALLS(Int)
        DEXRT_BIND_CALLS(Long)
        DEXRT_BIND_CALLS(Float)
        DEXRT_BIND_CALLS(Double)
        DEXRT_BIND_CALLS(Void)
#undef DEXRT_BIND_CALLS

#if defined(__aarch64__)
        fns.NewObject = reinterpret_cast<jobject (JNICALL *)(JNIEnv*, jclass, jmethodID, ...)>(kudroid_jni_new_object_trampoline);

        fns.CallVoidMethod = reinterpret_cast<void (JNICALL *)(JNIEnv*, jobject, jmethodID, ...)>(kudroid_jni_call_virtual_trampoline);
        fns.CallObjectMethod = reinterpret_cast<jobject (JNICALL *)(JNIEnv*, jobject, jmethodID, ...)>(kudroid_jni_call_virtual_object_trampoline);
        fns.CallBooleanMethod = reinterpret_cast<jboolean (JNICALL *)(JNIEnv*, jobject, jmethodID, ...)>(kudroid_jni_call_virtual_trampoline);
        fns.CallByteMethod = reinterpret_cast<jbyte (JNICALL *)(JNIEnv*, jobject, jmethodID, ...)>(kudroid_jni_call_virtual_trampoline);
        fns.CallCharMethod = reinterpret_cast<jchar (JNICALL *)(JNIEnv*, jobject, jmethodID, ...)>(kudroid_jni_call_virtual_trampoline);
        fns.CallShortMethod = reinterpret_cast<jshort (JNICALL *)(JNIEnv*, jobject, jmethodID, ...)>(kudroid_jni_call_virtual_trampoline);
        fns.CallIntMethod = reinterpret_cast<jint (JNICALL *)(JNIEnv*, jobject, jmethodID, ...)>(kudroid_jni_call_virtual_trampoline);
        fns.CallLongMethod = reinterpret_cast<jlong (JNICALL *)(JNIEnv*, jobject, jmethodID, ...)>(kudroid_jni_call_virtual_trampoline);
        fns.CallFloatMethod = reinterpret_cast<jfloat (JNICALL *)(JNIEnv*, jobject, jmethodID, ...)>(kudroid_jni_call_virtual_float_trampoline);
        fns.CallDoubleMethod = reinterpret_cast<jdouble (JNICALL *)(JNIEnv*, jobject, jmethodID, ...)>(kudroid_jni_call_virtual_double_trampoline);

        fns.CallStaticVoidMethod = reinterpret_cast<void (JNICALL *)(JNIEnv*, jclass, jmethodID, ...)>(kudroid_jni_call_static_trampoline);
        fns.CallStaticObjectMethod = reinterpret_cast<jobject (JNICALL *)(JNIEnv*, jclass, jmethodID, ...)>(kudroid_jni_call_static_object_trampoline);
        fns.CallStaticBooleanMethod = reinterpret_cast<jboolean (JNICALL *)(JNIEnv*, jclass, jmethodID, ...)>(kudroid_jni_call_static_trampoline);
        fns.CallStaticByteMethod = reinterpret_cast<jbyte (JNICALL *)(JNIEnv*, jclass, jmethodID, ...)>(kudroid_jni_call_static_trampoline);
        fns.CallStaticCharMethod = reinterpret_cast<jchar (JNICALL *)(JNIEnv*, jclass, jmethodID, ...)>(kudroid_jni_call_static_trampoline);
        fns.CallStaticShortMethod = reinterpret_cast<jshort (JNICALL *)(JNIEnv*, jclass, jmethodID, ...)>(kudroid_jni_call_static_trampoline);
        fns.CallStaticIntMethod = reinterpret_cast<jint (JNICALL *)(JNIEnv*, jclass, jmethodID, ...)>(kudroid_jni_call_static_trampoline);
        fns.CallStaticLongMethod = reinterpret_cast<jlong (JNICALL *)(JNIEnv*, jclass, jmethodID, ...)>(kudroid_jni_call_static_trampoline);
        fns.CallStaticFloatMethod = reinterpret_cast<jfloat (JNICALL *)(JNIEnv*, jclass, jmethodID, ...)>(kudroid_jni_call_static_float_trampoline);
        fns.CallStaticDoubleMethod = reinterpret_cast<jdouble (JNICALL *)(JNIEnv*, jclass, jmethodID, ...)>(kudroid_jni_call_static_double_trampoline);

        fns.CallNonvirtualVoidMethod = reinterpret_cast<void (JNICALL *)(JNIEnv*, jobject, jclass, jmethodID, ...)>(kudroid_jni_call_nonvirtual_trampoline);
        fns.CallNonvirtualObjectMethod = reinterpret_cast<jobject (JNICALL *)(JNIEnv*, jobject, jclass, jmethodID, ...)>(kudroid_jni_call_nonvirtual_object_trampoline);
        fns.CallNonvirtualBooleanMethod = reinterpret_cast<jboolean (JNICALL *)(JNIEnv*, jobject, jclass, jmethodID, ...)>(kudroid_jni_call_nonvirtual_trampoline);
        fns.CallNonvirtualByteMethod = reinterpret_cast<jbyte (JNICALL *)(JNIEnv*, jobject, jclass, jmethodID, ...)>(kudroid_jni_call_nonvirtual_trampoline);
        fns.CallNonvirtualCharMethod = reinterpret_cast<jchar (JNICALL *)(JNIEnv*, jobject, jclass, jmethodID, ...)>(kudroid_jni_call_nonvirtual_trampoline);
        fns.CallNonvirtualShortMethod = reinterpret_cast<jshort (JNICALL *)(JNIEnv*, jobject, jclass, jmethodID, ...)>(kudroid_jni_call_nonvirtual_trampoline);
        fns.CallNonvirtualIntMethod = reinterpret_cast<jint (JNICALL *)(JNIEnv*, jobject, jclass, jmethodID, ...)>(kudroid_jni_call_nonvirtual_trampoline);
        fns.CallNonvirtualLongMethod = reinterpret_cast<jlong (JNICALL *)(JNIEnv*, jobject, jclass, jmethodID, ...)>(kudroid_jni_call_nonvirtual_trampoline);
        fns.CallNonvirtualFloatMethod = reinterpret_cast<jfloat (JNICALL *)(JNIEnv*, jobject, jclass, jmethodID, ...)>(kudroid_jni_call_nonvirtual_float_trampoline);
        fns.CallNonvirtualDoubleMethod = reinterpret_cast<jdouble (JNICALL *)(JNIEnv*, jobject, jclass, jmethodID, ...)>(kudroid_jni_call_nonvirtual_double_trampoline);
#endif

        fns.GetFieldID = jnifns::GetFieldID;
        fns.GetStaticMethodID = jnifns::GetStaticMethodID;
        fns.GetStaticFieldID = jnifns::GetStaticFieldID;

#define DEXRT_BIND_FIELD(NAME)                                          \
        fns.Get##NAME##Field = jnifns::Get##NAME##Field;                \
        fns.Set##NAME##Field = jnifns::Set##NAME##Field;                 \
        fns.GetStatic##NAME##Field = jnifns::GetStatic##NAME##Field;     \
        fns.SetStatic##NAME##Field = jnifns::SetStatic##NAME##Field;

        DEXRT_BIND_FIELD(Object)
        DEXRT_BIND_FIELD(Boolean)
        DEXRT_BIND_FIELD(Byte)
        DEXRT_BIND_FIELD(Char)
        DEXRT_BIND_FIELD(Short)
        DEXRT_BIND_FIELD(Int)
        DEXRT_BIND_FIELD(Long)
        DEXRT_BIND_FIELD(Float)
        DEXRT_BIND_FIELD(Double)
#undef DEXRT_BIND_FIELD

        fns.NewString = jnifns::NewString;
        fns.GetStringLength = jnifns::GetStringLength;
        fns.GetStringChars = jnifns::GetStringChars;
        fns.ReleaseStringChars = jnifns::ReleaseStringChars;
        fns.NewStringUTF = jnifns::NewStringUTF;
        fns.GetStringUTFLength = jnifns::GetStringUTFLength;
        fns.GetStringUTFChars = jnifns::GetStringUTFChars;
        fns.ReleaseStringUTFChars = jnifns::ReleaseStringUTFChars;
        fns.GetStringRegion = jnifns::GetStringRegion;
        fns.GetStringUTFRegion = jnifns::GetStringUTFRegion;
        fns.GetStringCritical = jnifns::GetStringCritical;
        fns.ReleaseStringCritical = jnifns::ReleaseStringCritical;
        fns.GetStringUTFLengthAsLong = jnifns::GetStringUTFLengthAsLong;

        fns.GetArrayLength = jnifns::GetArrayLength;
        fns.NewObjectArray = jnifns::NewObjectArray;
        fns.GetObjectArrayElement = jnifns::GetObjectArrayElement;
        fns.SetObjectArrayElement = jnifns::SetObjectArrayElement;

#define DEXRT_BIND_ARRAY(NAME)                                                          \
        fns.New##NAME##Array = jnifns::New##NAME##Array;                                \
        fns.Get##NAME##ArrayElements = jnifns::Get##NAME##ArrayElements;                 \
        fns.Release##NAME##ArrayElements = jnifns::Release##NAME##ArrayElements;         \
        fns.Get##NAME##ArrayRegion = jnifns::Get##NAME##ArrayRegion;                     \
        fns.Set##NAME##ArrayRegion = jnifns::Set##NAME##ArrayRegion;

        DEXRT_BIND_ARRAY(Boolean)
        DEXRT_BIND_ARRAY(Byte)
        DEXRT_BIND_ARRAY(Char)
        DEXRT_BIND_ARRAY(Short)
        DEXRT_BIND_ARRAY(Int)
        DEXRT_BIND_ARRAY(Long)
        DEXRT_BIND_ARRAY(Float)
        DEXRT_BIND_ARRAY(Double)
#undef DEXRT_BIND_ARRAY

        fns.RegisterNatives = jnifns::RegisterNatives;
        fns.UnregisterNatives = jnifns::UnregisterNatives;
        fns.MonitorEnter = jnifns::MonitorEnter;
        fns.MonitorExit = jnifns::MonitorExit;
        fns.GetJavaVM = jnifns::GetJavaVM;
        fns.GetPrimitiveArrayCritical = jnifns::GetPrimitiveArrayCritical;
        fns.ReleasePrimitiveArrayCritical = jnifns::ReleasePrimitiveArrayCritical;
        fns.NewWeakGlobalRef = jnifns::NewWeakGlobalRef;
        fns.DeleteWeakGlobalRef = jnifns::DeleteWeakGlobalRef;
        fns.ExceptionCheck = jnifns::ExceptionCheck;
        fns.NewDirectByteBuffer = jnifns::NewDirectByteBuffer;
        fns.GetDirectBufferAddress = jnifns::GetDirectBufferAddress;
        fns.GetDirectBufferCapacity = jnifns::GetDirectBufferCapacity;
        fns.GetObjectRefType = jnifns::GetObjectRefType;
        fns.GetModule = jnifns::GetModule;
        fns.IsVirtualThread = jnifns::IsVirtualThread;
        fns.HasIdentity = jnifns::HasIdentity;

        vm_fns.DestroyJavaVM = jnifns::DestroyJavaVM;
        vm_fns.AttachCurrentThread = jnifns::AttachCurrentThread;
        vm_fns.DetachCurrentThread = jnifns::DetachCurrentThread;
        vm_fns.GetEnv = jnifns::GetEnv;
        vm_fns.AttachCurrentThreadAsDaemon = jnifns::AttachCurrentThreadAsDaemon;

        initialized = true;
    }

    env_storage_.functions = &fns;
    env_storage_.self = this;
    vm_storage_.functions = &vm_fns;
    vm_storage_.self = this;
}

}  // namespace kuart
}  // namespace kudroid
