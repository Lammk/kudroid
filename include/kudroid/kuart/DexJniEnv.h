// JNIEnv for KuART: bridge between DEX bytecode and native code ARM64 c a game.
//
// In place of the ~230 vtable functions that Avian/ART provides. This is the biggest part of
// KuART: game g i JNI li n t c (FindClass, GetMethodID, CallVoidMethod,
// RegisterNatives...) trong khi bytecode ch  ch y l c onCreate + touch event.
//
// nh x  handle JNI sang con tr  KuART:
// jclass    -> DexClass*   (qua b ng local/global ref)
//   jobject   -> DexObject*
// jmethodID -> DexMethod*  (con tr  tr c ti p, kh ng qua b ng   ID ph i b n)
//   jfieldID  -> DexField*
//
// Local ref d ng b ng theo frame   DeleteLocalRef v  PopLocalFrame ho t  ng
// ng; global ref c  b ng ri ng kh ng b  xo  theo frame.
#ifndef KUDROID_KUART_DEXJNIENV_H
#define KUDROID_KUART_DEXJNIENV_H

#include <jni.h>

#include <cstdarg>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "kudroid/kuart/DexClassLinker.h"
#include "kudroid/kuart/Interpreter.h"

namespace kudroid {
namespace kuart {

// Number of argument registers the trampoline below fills. AAPCS64 gives x0-x7
// and v0-v7; SysV x86-64 gives 6 GP and 8 SSE, so the GP budget is the smaller
// of the two to keep one call path valid on both (host tests run on x86-64 while
// the product target is arm64).
#if defined(__x86_64__)
constexpr unsigned kJniGpRegs = 6;
#else
constexpr unsigned kJniGpRegs = 8;
#endif
constexpr unsigned kJniFpRegs = 8;

}  // namespace kuart
}  // namespace kudroid

extern "C" {
// Append a SIGKILL-surviving breadcrumb to the host log directory.
void kudroid_persistent_breadcrumb(const char* line);
// Calls `fn` with the given register files. See src/kuart/jni_trampoline.S for
// the contract; `gp` and `fp` must each have 8 initialised slots. The integer
// return comes back directly, the float/double return is written to *fp_ret as
// raw bits (nullptr to discard).
uint64_t kudroid_jni_call(const void* fn, const uint64_t* gp, unsigned ngp,
                          const uint64_t* fp, unsigned nfp, uint64_t* fp_ret);
}

namespace kudroid {
namespace kuart {

class DexJniEnv {
public:
    DexJniEnv(DexClassLinker* linker, Interpreter* interpreter);
    ~DexJniEnv();

    DexJniEnv(const DexJniEnv&) = delete;
    DexJniEnv& operator=(const DexJniEnv&) = delete;

    // Con tr  truy n cho m  native. Layout kh p JNIEnv_ n n native cast  c.
    JNIEnv* env() { return reinterpret_cast<JNIEnv*>(&env_storage_); }
    JavaVM* vm() { return reinterpret_cast<JavaVM*>(&vm_storage_); }

    static DexJniEnv* FromEnv(JNIEnv* env);
    static DexJniEnv* FromVm(JavaVM* vm);

    DexClassLinker* linker() { return linker_; }
    Interpreter* interpreter() { return interpreter_; }

    // qu n l  reference
    jobject AddLocalRef(DexObject* obj);
    jobject AddGlobalRef(DexObject* obj);
    void DeleteLocalRef(jobject ref);
    void DeleteGlobalRef(jobject ref);
    static DexObject* Decode(jobject ref);

    void PushLocalFrame();
    void PopLocalFrame();

    // li n k t method native
    // RegisterNatives t  JNI_OnLoad c a game.
    jint RegisterNatives(DexClass* klass, const JNINativeMethod* methods, jint count);

    // T m h m native theo quy  c t n JNI (Java_pkg_Class_method) qua hook do
    // KuDroid c p   tr  t i LibraryManager c a guest .so.
    using SymbolLookup = void* (*)(const char* symbol);
    void set_symbol_lookup(SymbolLookup fn) { symbol_lookup_ = fn; }

    // Li n k t method native ch a c  native_fn. Tr  false n u not found.
    bool LinkNativeMethod(DexMethod* method);

    // G i method native   li n k t. args KH NG g m JNIEnv/jclass   h m n y t  th m.
    DexValue CallNative(DexMethod* method, const DexValue* args, size_t num_args);

    // g i method Java t  native
    // `receiver` null cho method static. `virtual_dispatch` = ch n l i b n
    // override theo class th t c a receiver (Call<Type>Method), t t cho
    // CallNonvirtual<Type>Method v  CallStatic<Type>Method.
    DexValue CallJavaA(DexObject* receiver, DexMethod* method, const jvalue* args,
                       bool virtual_dispatch);
    DexValue CallJavaV(DexObject* receiver, DexMethod* method, va_list args,
                       bool virtual_dispatch);

    DexObject* NewObjectA(DexClass* klass, DexMethod* ctor, const jvalue* args);

    static const char* MethodShorty(const DexMethod* method);

    // ── exception ──
    // Exception do bytecode n m n m   Interpreter, do native n m n m    y;
    // ba h m n y h p nh t hai ngu n   native ch  th y m t tr ng th i.
    void SetPendingException(DexObject* ex);
    DexObject* pending_exception() const;
    void ClearException();

    const std::string& last_error() const { return last_error_; }
    void set_last_error(const std::string& e) { last_error_ = e; }

    size_t NumLocalRefs() const;
    size_t NumGlobalRefs() const { return global_refs_.size(); }
    bool IsGlobalRef(DexObject* obj) const { return global_refs_.count(obj) != 0; }

private:
    void InitFunctionTable();

    // B  c c kh p JNIEnv_ / JavaVM_   tr ng  U TI N (con tr  b ng h m)
    // native ch   c tr ng  . Tr ng `self` ph a sau   FromEnv/FromVm quay
    // ng c v  DexJniEnv m  kh ng c n b ng tra c u to n c c.
    struct EnvStorage {
        const JNINativeInterface_* functions = nullptr;
        DexJniEnv* self = nullptr;
    };
    struct VmStorage {
        const JNIInvokeInterface_* functions = nullptr;
        DexJniEnv* self = nullptr;
    };

    EnvStorage env_storage_{};
    VmStorage vm_storage_{};

    DexClassLinker* linker_ = nullptr;
    Interpreter* interpreter_ = nullptr;
    SymbolLookup symbol_lookup_ = nullptr;

    // M i frame l  m t t p local ref. Frame ngo i c ng lu n t n t i.
    std::vector<std::vector<DexObject*>> local_frames_;
    std::unordered_set<DexObject*> global_refs_;

    DexObject* pending_exception_ = nullptr;
    std::string last_error_;
};

}  // namespace kuart
}  // namespace kudroid

#endif  // KUDROID_KUART_DEXJNIENV_H
