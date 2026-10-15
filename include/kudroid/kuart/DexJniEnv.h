// JNIEnv for KuART: bridge between DEX bytecode and game native code.
// Replaces ~230 ART vtable functions; maps JNI handles to KuART pointers.
// Local refs are per-frame; global refs live in a separate table.
#ifndef KUDROID_KUART_DEXJNIENV_H
#define KUDROID_KUART_DEXJNIENV_H

#include <jni.h>

#include <cstdarg>
#include <mutex>
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

    // Pointer handed to native code; layout matches JNIEnv_.
    JNIEnv* env() { return reinterpret_cast<JNIEnv*>(&env_storage_); }
    JavaVM* vm() { return reinterpret_cast<JavaVM*>(&vm_storage_); }

    static DexJniEnv* FromEnv(JNIEnv* env);
    static DexJniEnv* FromVm(JavaVM* vm);

    DexClassLinker* linker() { return linker_; }
    Interpreter* interpreter() { return interpreter_; }

    // Reference management
    jobject AddLocalRef(DexObject* obj);
    jobject AddGlobalRef(DexObject* obj);
    void DeleteLocalRef(jobject ref);
    void DeleteGlobalRef(jobject ref);
    static DexObject* Decode(jobject ref);

    void PushLocalFrame();
    void PopLocalFrame();

    // Native method binding (RegisterNatives from game JNI_OnLoad).
    jint RegisterNatives(DexClass* klass, const JNINativeMethod* methods, jint count);

    // Find native funcs by JNI name convention via the guest LibraryManager hook.
    using SymbolLookup = void* (*)(const char* symbol);
    void set_symbol_lookup(SymbolLookup fn) { symbol_lookup_ = fn; }

    // Bind a native method lacking native_fn; false when not found.
    bool LinkNativeMethod(DexMethod* method);

    // Call a bound native; args exclude JNIEnv/jclass (added here).
    DexValue CallNative(DexMethod* method, const DexValue* args, size_t num_args);

    // Call Java from native; null receiver for static. virtual_dispatch selects the override.
    DexValue CallJavaA(DexObject* receiver, DexMethod* method, const jvalue* args,
                       bool virtual_dispatch);
    DexValue CallJavaV(DexObject* receiver, DexMethod* method, va_list args,
                       bool virtual_dispatch);

    DexObject* NewObjectA(DexClass* klass, DexMethod* ctor, const jvalue* args);

    static const char* MethodShorty(const DexMethod* method);

    // ── exception ──
    // Exceptions from bytecode live in the Interpreter, from native here; merged to one state.
    void SetPendingException(DexObject* ex);
    DexObject* pending_exception() const;
    void ClearException();

    const std::string& last_error() const { return last_error_; }
    void set_last_error(const std::string& e) { last_error_ = e; }

    size_t NumLocalRefs() const;
    size_t NumGlobalRefs() const {
        std::lock_guard<std::mutex> lock(global_refs_mutex_);
        return global_refs_.size();
    }
    bool IsGlobalRef(DexObject* obj) const {
        std::lock_guard<std::mutex> lock(global_refs_mutex_);
        return global_refs_.count(obj) != 0;
    }

private:
    void InitFunctionTable();

    // Layout matches JNIEnv_/JavaVM_ (function table first); trailing `self` maps back.
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

    // One DexJniEnv serves every guest thread (see KuArtRuntime), so the
    // GLOBAL table below is shared across threads — UnityMain, the UI
    // thread posting touch events, FMOD's mixer — and every accessor takes
    // global_refs_mutex_ (the jni_globals_lock_ of this runtime). An
    // unguarded std::unordered_set insert from two threads corrupts buckets
    // into garbage pointers (observed: AddGlobalRef fault on a touch event).
    // Leaf-level only: no other lock is ever held across these calls, so no
    // lock ordering exists to violate.
    //
    // Local frames and the pending exception are NOT here: like ART's
    // JNIEnvExt/ExceptionState they are thread-confined (thread_local in
    // DexJniEnv.cpp), lock-free, and invisible across threads. The
    // interpreter's own pending_exception_ is already thread_local, and the
    // Set/pending/Clear trio below forwards to it — both slots are TLS so
    // the merge stays thread-confined on every thread.
    std::unordered_set<DexObject*> global_refs_;
    mutable std::mutex global_refs_mutex_;

    std::string last_error_;
};

// True while Unity's JNIBridge.invoke runs, enabling scoped JNI tracing.
bool JnibridgeTraceActive();

}  // namespace kuart
}  // namespace kudroid

#endif  // KUDROID_KUART_DEXJNIENV_H
