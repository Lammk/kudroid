#include "kudroid/kuart/DexJniEnv.h"

#include <chrono>

#include "kudroid/Log.h"
#include "kudroid/NativeCallTelemetry.h"
#include "kudroid/platform/MemoryInfo.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <set>
#include <string>

#include "dex/dex_file-inl.h"
#include "dex/modifiers.h"

#include "kudroid/kuart/DexClassLinker.h"
#include "kudroid/kuart/DexClassObject.h"
#include "kudroid/kuart/LibCore.h"
#include "kudroid/kuart/VmLock.h"

namespace kudroid {
namespace kuart {

namespace {

// jobject is an opaque handle; KuART directly uses the DexObject pointer as a handle. Board
// local/global refs are only for lifetime management, not for address translation - so
// Decode ch  l  m t cast.  n gi n h n ART (d ng indirect reference table) v
// enough because there is no GC to move the object.
DexObject* AsObject(jobject ref) { return reinterpret_cast<DexObject*>(ref); }
jobject AsHandle(DexObject* obj) { return reinterpret_cast<jobject>(obj); }

// Encode method names according to JNI convention: Java_<pkg>_<Class>_<method>.
// '_' -> "_1", '/' -> '_', ';' -> "_2", '[' -> "_3".
std::string MangleJniName(const char* descriptor, const char* method_name) {
    std::string out = "Java_";
    // descriptor of the form "Lcom/foo/Bar;" - remove the leading 'L' and ';' last.
    const size_t len = std::strlen(descriptor);
    size_t start = (len > 0 && descriptor[0] == 'L') ? 1 : 0;
    size_t end = (len > 0 && descriptor[len - 1] == ';') ? len - 1 : len;
    for (size_t i = start; i < end; ++i) {
        const char c = descriptor[i];
        if (c == '/') {
            out += '_';
        } else if (c == '_') {
            out += "_1";
        } else if (c == '$') {
            out += "_00024";
        } else {
            out += c;
        }
    }
    out += '_';
    for (const char* p = method_name; *p != '\0'; ++p) {
        if (*p == '_') {
            out += "_1";
        } else {
            out += *p;
        }
    }
    return out;
}

// The long version has a signature suffix, used when there are multiple overloads with the same name.
std::string MangleJniNameLong(const char* descriptor, const char* method_name,
                             const char* signature) {
    std::string out = MangleJniName(descriptor, method_name);
    out += "__";
    // Parameters only, return type omitted.
    for (const char* p = signature; *p != '\0' && *p != ')'; ++p) {
        if (*p == '(') continue;
        if (*p == '/') {
            out += '_';
        } else if (*p == '_') {
            out += "_1";
        } else if (*p == ';') {
            out += "_2";
        } else if (*p == '[') {
            out += "_3";
        } else if (*p == '$') {
            out += "_00024";
        } else {
            out += *p;
        }
    }
    return out;
}

}  // namespace

DexObject* DexJniEnv::Decode(jobject ref) { return AsObject(ref); }

DexJniEnv* DexJniEnv::FromEnv(JNIEnv* env) {
    if (env == nullptr) return nullptr;
    return reinterpret_cast<EnvStorage*>(env)->self;
}

thread_local bool t_jnibridge_trace = false;

bool JnibridgeTraceActive() { return t_jnibridge_trace; }

// RAII guard: restores the previous state so a nested native call (a Java callback
// made inside invoke calling back out to another native) cannot leak the flag.
struct JnibridgeTraceGuard {
    bool prev;
    explicit JnibridgeTraceGuard(bool on) : prev(t_jnibridge_trace) {
        if (on) t_jnibridge_trace = true;
    }
    ~JnibridgeTraceGuard() { t_jnibridge_trace = prev; }
};

DexJniEnv* DexJniEnv::FromVm(JavaVM* vm) {
    if (vm == nullptr) return nullptr;
    return reinterpret_cast<VmStorage*>(vm)->self;
}

const char* DexJniEnv::MethodShorty(const DexMethod* method) {
    if (method == nullptr || method->dex_file == nullptr) return nullptr;
    return method->dex_file->GetMethodShorty(
        method->dex_file->GetMethodId(method->dex_method_index));
}

DexJniEnv::DexJniEnv(DexClassLinker* linker, Interpreter* interpreter)
    : linker_(linker), interpreter_(interpreter) {
    local_frames_.emplace_back();
    InitFunctionTable();
}

DexJniEnv::~DexJniEnv() = default;

jobject DexJniEnv::AddLocalRef(DexObject* obj) {
    if (obj == nullptr) return nullptr;
    if (local_frames_.empty()) local_frames_.emplace_back();
    local_frames_.back().push_back(obj);
    return AsHandle(obj);
}

jobject DexJniEnv::AddGlobalRef(DexObject* obj) {
    if (obj == nullptr) return nullptr;
    global_refs_.insert(obj);
    return AsHandle(obj);
}

void DexJniEnv::DeleteLocalRef(jobject ref) {
    DexObject* obj = AsObject(ref);
    if (obj == nullptr || local_frames_.empty()) return;
    auto& frame = local_frames_.back();
    for (auto it = frame.rbegin(); it != frame.rend(); ++it) {
        if (*it == obj) {
            frame.erase(std::next(it).base());
            return;
        }
    }
}

void DexJniEnv::DeleteGlobalRef(jobject ref) {
    DexObject* obj = AsObject(ref);
    if (obj != nullptr) global_refs_.erase(obj);
}

void DexJniEnv::PushLocalFrame() { local_frames_.emplace_back(); }

void DexJniEnv::PopLocalFrame() {
    // The outermost frame must always remain so that AddLocalRef does not need to check for nullity.
    if (local_frames_.size() > 1) local_frames_.pop_back();
}

size_t DexJniEnv::NumLocalRefs() const {
    size_t n = 0;
    for (const auto& frame : local_frames_) n += frame.size();
    return n;
}

void DexJniEnv::SetPendingException(DexObject* ex) {
    pending_exception_ = ex;
    if (interpreter_ != nullptr) interpreter_->SetPendingException(ex);
}

DexObject* DexJniEnv::pending_exception() const {
    if (pending_exception_ != nullptr) return pending_exception_;
    return interpreter_ != nullptr ? interpreter_->pending_exception() : nullptr;
}

void DexJniEnv::ClearException() {
    pending_exception_ = nullptr;
    if (interpreter_ != nullptr) interpreter_->ClearPendingException();
}

jint DexJniEnv::RegisterNatives(DexClass* klass, const JNINativeMethod* methods, jint count) {
    if (klass == nullptr || methods == nullptr) return JNI_ERR;

    jint failures = 0;
    for (jint i = 0; i < count; ++i) {
        const JNINativeMethod& m = methods[i];
        if (m.name == nullptr || m.signature == nullptr || m.fnPtr == nullptr) {
            ++failures;
            continue;
        }
        DexMethod* target = klass->FindDirectMethod(m.name, m.signature);
        if (target == nullptr) target = klass->FindVirtualMethod(m.name, m.signature);
        if (target == nullptr) {
            last_error_ = std::string("RegisterNatives: no method ") + m.name +
                          m.signature + " in " + klass->PrettyName();
            ++failures;
            continue;
        }
        target->native_fn = m.fnPtr;
    }
    return failures == 0 ? JNI_OK : JNI_ERR;
}

bool DexJniEnv::LinkNativeMethod(DexMethod* method) {
    if (method == nullptr) return false;
    if (method->native_fn != nullptr) return true;
    if (LibCoreHasMethod(method)) return true;
    if (symbol_lookup_ == nullptr || method->declaring_class == nullptr) return false;

    const char* descriptor = method->declaring_class->descriptor;
    // Try a short name first (no signature suffix) like Android's JNI linker.
    const std::string short_name = MangleJniName(descriptor, method->name);
    if (void* fn = symbol_lookup_(short_name.c_str())) {
        method->native_fn = fn;
        return true;
    }
    const std::string long_name =
        MangleJniNameLong(descriptor, method->name, method->signature);
    if (void* fn = symbol_lookup_(long_name.c_str())) {
        method->native_fn = fn;
        return true;
    }
    last_error_ = "native symbol not found: " + short_name;
    return false;
}

// One-line decode of Unity's JNIBridge.invoke(J, Class, Method, Object[]) arguments:
// the native peer pointer, the interface Class, and the reflected Method (name and
// signature via its artMethod handle). Logged once at entry so a failing call can
// be told apart from a succeeding one without decoding raw pointers from the log.
void LogJnibridgeInvokeArgs(DexClassLinker* linker, const DexValue* args, size_t num_args) {
    if (linker == nullptr || args == nullptr || num_args < 4) {
        std::fprintf(stderr, "[KuART][JNIBRIDGE] invoke (undecodable args=%zu)\n", num_args);
        return;
    }
    const int64_t ptr = args[0].j;

    std::string cls_name = "?";
    if (DexObject* cls_obj = args[1].l) {
        DexClass* cls = linker->ClassFromObject(cls_obj);
        if (cls == nullptr &&
            linker->IsRegisteredClass(reinterpret_cast<const DexClass*>(cls_obj))) {
            cls = reinterpret_cast<DexClass*>(cls_obj);
        }
        if (cls != nullptr) cls_name = cls->PrettyName();
    }

    std::string method_name = "?";
    if (DexObject* m_obj = args[2].l) {
        if (m_obj->clazz != nullptr) {
            if (const DexField* f = m_obj->clazz->FindInstanceField("artMethod", "J")) {
                auto* m = reinterpret_cast<const DexMethod*>(
                    static_cast<uintptr_t>(m_obj->GetField<int64_t>(f->offset_or_slot)));
                if (m != nullptr) {
                    method_name = (m->declaring_class != nullptr)
                                      ? m->declaring_class->PrettyName()
                                      : "?";
                    method_name += ".";
                    method_name += m->name != nullptr ? m->name : "?";
                    method_name += m->signature != nullptr ? m->signature : "?";
                } else {
                    method_name = "(null artMethod)";
                }
            }
        }
    }

    int nargs = -1;
    if (DexObject* arr_obj = args[3].l) {
        DexClass* ac = linker->ClassOfObject(arr_obj);
        if (ac != nullptr && ac->is_array) {
            nargs = static_cast<DexArray*>(arr_obj)->length;
        }
    }
    std::fprintf(stderr, "[KuART][JNIBRIDGE] invoke ptr=0x%llx class=%s method=%s nargs=%d\n",
                 static_cast<unsigned long long>(ptr), cls_name.c_str(),
                 method_name.c_str(), nargs);
}

DexValue DexJniEnv::CallNative(DexMethod* method, const DexValue* args, size_t num_args) {
    DexValue result;
    if (method == nullptr) return result;


    // self-written libcore without native_fn; Call directly in C++.
    if (LibCoreInvoke(interpreter_, method, args, num_args, &result)) return result;
    if (method->native_fn == nullptr) return result;

    // This breadcrumb is deliberately emitted before entering guest code.  A
    // SIGKILL does not run the crash handler, so the last durable line in the
    // Android log is often the only evidence of the operation in progress.
    const char* owner = (method->declaring_class != nullptr &&
                         method->declaring_class->descriptor != nullptr)
                            ? method->declaring_class->descriptor : "?";
    const char* method_name = method->name != nullptr ? method->name : "?";
    const char* method_sig = method->signature != nullptr ? method->signature : "?";
    // Trace every JNI call made while Unity's bridge runs: the bridge is a black
    // box (real libunity.so code) and the failing lookup inside it is invisible
    // otherwise. Scoped to this call's dynamic extent by the RAII guard.
    const bool is_jnibridge_invoke =
        std::strcmp(owner, "Lbitter/jnibridge/JNIBridge;") == 0 &&
        std::strcmp(method_name, "invoke") == 0;
    JnibridgeTraceGuard trace_guard(is_jnibridge_invoke);
    if (is_jnibridge_invoke) {
        LogJnibridgeInvokeArgs(interpreter_ != nullptr ? interpreter_->linker() : nullptr,
                               args, num_args);
    }
    const auto native_start = std::chrono::steady_clock::now();
    KLOGV("KuARTNative", "enter class=%s method=%s sig=%s args=%zu vm_depth=%d",
          owner, method_name, method_sig, num_args, VmLockDepth());
    char breadcrumb[2048];
    const SystemMemory memory_before = query_system_memory();
    std::snprintf(breadcrumb, sizeof(breadcrumb),
                  "native-enter class=%s method=%s sig=%s args=%zu vm_depth=%d footprint=%llu process_headroom=%llu available=%llu low_memory=%d",
                  owner, method_name, method_sig, num_args, VmLockDepth(),
                  static_cast<unsigned long long>(memory_before.process_resident_bytes),
                  static_cast<unsigned long long>(memory_before.process_available_bytes),
                  static_cast<unsigned long long>(memory_before.available_bytes),
                  memory_before.low_memory ? 1 : 0);
    kudroid_persistent_breadcrumb(breadcrumb);
    native_call_enter(owner, method_name, method_sig, VmLockDepth());

    const char* shorty = nullptr;
    if (method->dex_file != nullptr) {
        shorty = method->dex_file->GetMethodShorty(
            method->dex_file->GetMethodId(method->dex_method_index));
    }
    if (shorty == nullptr) {
        native_call_stage("invalid-shorty");
        native_call_exit();
        return result;
    }

    // Bypass crash-setup wait; handled by host telemetry, avoids deadlock.
    if (std::strcmp(method_name, "nativeWaitCrashManagementSetupComplete") == 0) {
        KLOGV("KuARTNative", "bypassing nativeWaitCrashManagementSetupComplete -> returning immediately");
        native_call_exit();
        return result;
    }

    // Bypass network-status wait; avoids onCreate circular deadlock.
    if (std::strcmp(method_name, "nativeUpdateNetworkStatus") == 0) {
        KLOGV("KuARTNative", "bypassing nativeUpdateNetworkStatus -> returning immediately to prevent onCreate circular deadlock");
        native_call_exit();
        return result;
    }

    // Xbox HttpClient NetworkObserver.Log is a pure diagnostic log callback from Java
    // to libHttpClient.Android.so. Inside libHttpClient, calling this on the main thread
    // blocks on an uninitialized internal logging sink or mutex before HttpClient is initialized.
    // Returning immediately avoids this hang.
    if (std::strcmp(method_name, "Log") == 0 && std::strstr(owner, "NetworkObserver") != nullptr) {
        KLOGV("KuARTNative", "bypassing NetworkObserver.Log -> returning immediately");
        native_call_exit();
        return result;
    }

    // Unity's NativeLoader verifies that native libraries (libmain.so, libunity.so) are loaded.
    // In KuDroid, all ELF libraries are pre-loaded by the core runtime before Java startup.
    // Returning true (1) confirms library availability so UnityPlayer proceeds to native initialization.
    if (std::strstr(owner, "NativeLoader") != nullptr) {
        if (std::strcmp(method_name, "load") == 0 ||
            std::strcmp(method_name, "unload") == 0 ||
            std::strcmp(method_name, "initialize") == 0) {
            KLOGV("KuARTNative", "Unity NativeLoader.%s -> returning true", method_name);
            native_call_exit();
            return DexValue::Int(1);
        }
    }

    // The first parameter after env is receiver (instance) or jclass (static).
    void* self;
    size_t first = 0;
    if (method->IsStatic()) {
        self = reinterpret_cast<void*>(method->declaring_class);
    } else {
        self = num_args > 0 ? reinterpret_cast<void*>(args[0].l) : nullptr;
        first = 1;
    }

    // JNI ABI: (JNIEnv*, jclass|jobject, ...parameters). Integer/pointer arguments
    // travel in the general-purpose registers while float/double travel in the
    // SEPARATE FP register file - they consume independent budgets, so a method
    // taking many floats is fine even though it has many parameters.
    uint64_t gp[kJniGpRegs] = {0};
    uint64_t fp[kJniFpRegs] = {0};
    unsigned ngp = 0;
    unsigned nfp = 0;

    gp[ngp++] = reinterpret_cast<uint64_t>(env());
    gp[ngp++] = reinterpret_cast<uint64_t>(self);

    size_t arg_index = first;
    for (const char* p = shorty + 1; *p != '\0' && arg_index < num_args; ++p, ++arg_index) {
        switch (*p) {
            case 'F':
            case 'D':
                if (nfp >= kJniFpRegs) goto too_many_args;
                // A 'F' lives in the low 32 bits of its slot, which is what the
                // callee reads as s<N>; DexValue::Float already zeroes the rest.
                fp[nfp++] = args[arg_index].raw;
                break;
            case 'J':
                if (ngp >= kJniGpRegs) goto too_many_args;
                gp[ngp++] = static_cast<uint64_t>(args[arg_index].j);
                break;
            case 'L':
            case '[':
                if (ngp >= kJniGpRegs) goto too_many_args;
                gp[ngp++] = reinterpret_cast<uint64_t>(args[arg_index].l);
                break;
            default:
                if (ngp >= kJniGpRegs) goto too_many_args;
                // Narrow integer types are passed zero/sign-extended to 32 bits
                // then widened; the callee only looks at w<N>.
                gp[ngp++] = static_cast<uint64_t>(
                    static_cast<uint32_t>(args[arg_index].i));
                break;
        }
        continue;
    too_many_args:
        // Throw for stack-passed args; silently returning 0 hides failures.
        if (interpreter_ != nullptr) {
            interpreter_->ThrowException(
                "Ljava/lang/UnsatisfiedLinkError;",
                std::string("native method needs stack-passed arguments, not supported: ") +
                    (method->name != nullptr ? method->name : "?"));
        }
        last_error_ = std::string("native method needs stack-passed arguments: ") +
                      (method->name != nullptr ? method->name : "?");
        native_call_stage("unsupported-stack-args");
        native_call_exit();
        return result;
    }

    uint64_t fp_ret = 0;
    uint64_t ret;
    {
        // Run native with VM lock released so blocking calls and callbacks work.
        native_call_stage("before-vm-release");
        VmLockRelease unlocked;
        native_call_stage("before-trampoline");
        ret = kudroid_jni_call(method->native_fn, gp, ngp, fp, nfp, &fp_ret);
        native_call_stage("after-trampoline");
    }

    const auto native_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - native_start).count();
    KLOGV("KuARTNative", "exit class=%s method=%s sig=%s duration_ms=%lld vm_depth=%d",
          owner, method_name, method_sig, static_cast<long long>(native_ms), VmLockDepth());
    const SystemMemory memory_after = query_system_memory();
    std::snprintf(breadcrumb, sizeof(breadcrumb),
                  "native-exit class=%s method=%s sig=%s duration_ms=%lld vm_depth=%d footprint=%llu process_headroom=%llu available=%llu low_memory=%d",
                  owner, method_name, method_sig, static_cast<long long>(native_ms), VmLockDepth(),
                  static_cast<unsigned long long>(memory_after.process_resident_bytes),
                  static_cast<unsigned long long>(memory_after.process_available_bytes),
                  static_cast<unsigned long long>(memory_after.available_bytes),
                  memory_after.low_memory ? 1 : 0);
    kudroid_persistent_breadcrumb(breadcrumb);
    native_call_stage("before-result-decode");
    switch (shorty[0]) {
        case 'V': break;
        case 'Z': result = DexValue::Int(ret != 0 ? 1 : 0); break;
        case 'B': result = DexValue::Int(static_cast<int8_t>(ret)); break;
        case 'C': result = DexValue::Int(static_cast<uint16_t>(ret)); break;
        case 'S': result = DexValue::Int(static_cast<int16_t>(ret)); break;
        case 'I': result = DexValue::Int(static_cast<int32_t>(ret)); break;
        case 'J': result = DexValue::Long(static_cast<int64_t>(ret)); break;
        case 'F': {
            // The FP return arrives in v0/xmm0, captured raw by the trampoline.
            // A float occupies the low 32 bits of that register.
            float f;
            const uint32_t bits = static_cast<uint32_t>(fp_ret);
            std::memcpy(&f, &bits, sizeof(f));
            result = DexValue::Float(f);
            break;
        }
        case 'D': {
            double d;
            std::memcpy(&d, &fp_ret, sizeof(d));
            result = DexValue::Double(d);
            break;
        }
        case 'L':
        case '[':
            result = DexValue::Ref(reinterpret_cast<DexObject*>(ret));
            break;
        default:
            last_error_ = std::string("unknown return shorty '") + shorty[0] + "' in " +
                          (method->name != nullptr ? method->name : "?");
            break;
    }
    // TEMP DIAGNOSTIC (ULTRAKILL render stall): Unity's frame loop lives or dies
    // on UnityPlayer.nativeRender()'s boolean - false quits the game. Log it.
    if (std::strcmp(owner, "Lcom/unity3d/player/UnityPlayer;") == 0 && shorty != nullptr &&
        (shorty[0] == 'Z' || shorty[0] == 'I')) {
        std::fprintf(stderr, "[KuART][UNITYPLAYER] %s%s -> %d\n", method_name,
                     method_sig, result.i);
    }
    native_call_exit();
    return result;
}

DexValue DexJniEnv::CallJavaA(DexObject* receiver, DexMethod* method, const jvalue* args,
                              bool virtual_dispatch) {
    DexValue result;
    if (method == nullptr || interpreter_ == nullptr) {
        // Was silent: native got null/0 back with no pending exception, returned
        // OK to Java, and Unity's JNIBridge wrapper threw a messageless
        // NoSuchMethodError far from here. Name the call site instead.
        std::fprintf(stderr, "[KuART][JNI] CallJavaA with null method (receiver=%p)\n",
                     reinterpret_cast<const void*>(receiver));
        return result;
    }
    if (t_jnibridge_trace) {
        std::string recv = "(null)";
        if (receiver != nullptr && linker_ != nullptr) {
            if (DexClass* rc = linker_->ClassOfObject(receiver)) recv = rc->PrettyName();
        }
        std::fprintf(stderr, "[KuART][JNIBRIDGE] CallJavaA %s.%s%s virtual=%d receiver=%s\n",
                     method->declaring_class != nullptr
                         ? method->declaring_class->PrettyName().c_str()
                         : "?",
                     method->name != nullptr ? method->name : "?",
                     method->signature != nullptr ? method->signature : "?",
                     virtual_dispatch ? 1 : 0, recv.c_str());
    }

    // Validate native-supplied receiver; fall back to non-virtual on bad handles.
    if (receiver != nullptr && linker_ != nullptr &&
        linker_->IsRegisteredClass(reinterpret_cast<const DexClass*>(receiver))) {        // A jclass receiver is not a mistake. In the JNI object model a jclass IS the
        // A jclass is a valid Class object; substitute the heap instance.
        if (DexClassObject* as_object = linker_->GetClassObject(
                const_cast<DexClass*>(reinterpret_cast<const DexClass*>(receiver)))) {
            receiver = as_object;
        }
    }

    if (virtual_dispatch && receiver != nullptr && linker_ != nullptr) {
        if (DexClass* receiver_class = linker_->ClassOfObject(receiver)) {
            DexMethod* found = receiver_class->FindVirtualMethod(method->name, method->signature);
            if (found != nullptr) {
                method = found;
            } else {
                // Was silent: falling through to a non-virtual call on the
                // interface/abstract method (e.g. Runnable.run on a Proxy whose
                // vtable lacks it) executes a bodiless method and dies far away.
                // Log the miss so the receiver/method pair is on record.
                const char* recv_name = (receiver_class->descriptor != nullptr)
                                            ? receiver_class->descriptor
                                            : "?";
                std::fprintf(stderr,
                             "[KuART][JNI] virtual dispatch MISS %s.%s%s on receiver %s\n",
                             (method->declaring_class != nullptr &&
                              method->declaring_class->descriptor != nullptr)
                                 ? method->declaring_class->descriptor
                                 : "?",
                             method->name != nullptr ? method->name : "?",
                             method->signature != nullptr ? method->signature : "",
                             recv_name);
            }
        } else {
            // Report it once per method: silently degrading to a non-virtual call
            // hides that a library is handing over bad handles, and the wrong
            // override may then run for the rest of the session.
            static std::mutex s_mtx;
            static std::set<const DexMethod*> s_reported;
            bool first = false;
            {
                std::lock_guard<std::mutex> lock(s_mtx);
                first = s_reported.insert(method).second;
            }
            if (first) {
                std::fprintf(stderr,
                             "[KuART][JNI] ⚠ virtual dispatch skipped for %s%s: %s\n",
                             method->name != nullptr ? method->name : "?",
                             method->signature != nullptr ? method->signature : "",
                             linker_->DescribeBadReceiver(receiver).c_str());
            }
        }
    }

    std::vector<DexValue> vals;
    if (!method->IsStatic()) vals.push_back(DexValue::Ref(receiver));

    // jvalue is a 64-bit union so long/double still only takes up ONE args element,
    // matches the interpreter's DexValue convention.
    const char* shorty = MethodShorty(method);
    if (shorty != nullptr && args != nullptr) {
        size_t i = 0;
        for (const char* p = shorty + 1; *p != '\0'; ++p, ++i) {
            switch (*p) {
                case 'Z': vals.push_back(DexValue::Int(args[i].z != 0 ? 1 : 0)); break;
                case 'B': vals.push_back(DexValue::Int(args[i].b)); break;
                case 'C': vals.push_back(DexValue::Int(args[i].c)); break;
                case 'S': vals.push_back(DexValue::Int(args[i].s)); break;
                case 'I': vals.push_back(DexValue::Int(args[i].i)); break;
                case 'J': vals.push_back(DexValue::Long(args[i].j)); break;
                case 'F': vals.push_back(DexValue::Float(args[i].f)); break;
                case 'D': vals.push_back(DexValue::Double(args[i].d)); break;
                default: {
                    // Validate guest-supplied reference args at the boundary.
                    DexObject* arg = AsObject(args[i].l);
                    if (arg != nullptr && linker_ != nullptr) {
                        const DexClassLinker::BadReceiver kind = linker_->ClassifyObject(arg);
                        if (kind == DexClassLinker::BadReceiver::kIsAClass) {
                            // A jclass is valid for Class params; substitute heap instance.
                            if (DexClassObject* as_object = linker_->GetClassObject(
                                    const_cast<DexClass*>(
                                        reinterpret_cast<const DexClass*>(arg)))) {
                                arg = as_object;
                            }
                        } else if (kind != DexClassLinker::BadReceiver::kOk) {
                            if (interpreter_ != nullptr) {
                                std::string detail = "JNI call to ";
                                if (method->declaring_class != nullptr) {
                                    detail += method->declaring_class->PrettyName();
                                    detail += ".";
                                }
                                detail += method->name != nullptr ? method->name : "?";
                                if (method->signature != nullptr) detail += method->signature;
                                detail += " — ";
                                detail += linker_->DescribeBadObject(
                                    arg, ("argument " + std::to_string(i)).c_str());
                                interpreter_->ThrowException(
                                    "Ljava/lang/IllegalArgumentException;", detail);
                            }
                            return result;
                        }
                    }
                    vals.push_back(DexValue::Ref(arg));
                    break;
                }
            }
        }
    }

    if (method->IsNative()) {
        if (!LinkNativeMethod(method)) return result;
        return CallNative(method, vals.data(), vals.size());
    }
    return interpreter_->Execute(method, vals.data(), vals.size());
}

DexValue DexJniEnv::CallJavaV(DexObject* receiver, DexMethod* method, va_list args,
                              bool virtual_dispatch) {
    DexValue result;
    if (method == nullptr) {
        std::fprintf(stderr, "[KuART][JNI] CallJavaV with null method (receiver=%p)\n",
                     reinterpret_cast<const void*>(receiver));
        return result;
    }

    // va_arg promote: any type smaller than int to int, float to double.
    std::vector<jvalue> boxed;
    const char* shorty = MethodShorty(method);
    if (shorty != nullptr) {
        for (const char* p = shorty + 1; *p != '\0'; ++p) {
            jvalue v;
            v.j = 0;
            switch (*p) {
                case 'Z': v.z = va_arg(args, jint) != 0 ? JNI_TRUE : JNI_FALSE; break;
                case 'B': v.b = static_cast<jbyte>(va_arg(args, jint)); break;
                case 'C': v.c = static_cast<jchar>(va_arg(args, jint)); break;
                case 'S': v.s = static_cast<jshort>(va_arg(args, jint)); break;
                case 'I': v.i = va_arg(args, jint); break;
                case 'J': v.j = va_arg(args, jlong); break;
                case 'F': v.f = static_cast<jfloat>(va_arg(args, jdouble)); break;
                case 'D': v.d = va_arg(args, jdouble); break;
                default: v.l = va_arg(args, jobject); break;
            }
            boxed.push_back(v);
        }
    }
    return CallJavaA(receiver, method, boxed.data(), virtual_dispatch);
}

DexObject* DexJniEnv::NewObjectA(DexClass* klass, DexMethod* ctor, const jvalue* args) {
    if (klass == nullptr || linker_ == nullptr) return nullptr;
    DexObject* obj = linker_->AllocObject(klass);
    if (obj == nullptr) return nullptr;
    if (ctor != nullptr) CallJavaA(obj, ctor, args, /*virtual_dispatch=*/false);
    return obj;
}

}  // namespace kuart
}  // namespace kudroid
