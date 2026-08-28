#include "kudroid/kuart/DexJniEnv.h"

#include <cstdlib>
#include <cstring>

#include "dex/dex_file-inl.h"
#include "dex/modifiers.h"

#include "kudroid/kuart/LibCore.h"

namespace kudroid {
namespace kuart {

namespace {

// jobject is an opaque handle; KuART directly uses the DexObject pointer as a handle. Board
// local/global refs are only for lifetime management, not for address translation — so
// Decode ch  l  m t cast.  n gi n h n ART (d ng indirect reference table) v
// enough because there is no GC to move the object.
DexObject* AsObject(jobject ref) { return reinterpret_cast<DexObject*>(ref); }
jobject AsHandle(DexObject* obj) { return reinterpret_cast<jobject>(obj); }

// Encode method names according to JNI convention: Java_<pkg>_<Class>_<method>.
// '_' -> "_1", '/' -> '_', ';' -> "_2", '[' -> "_3".
std::string MangleJniName(const char* descriptor, const char* method_name) {
    std::string out = "Java_";
    // descriptor of the form "Lcom/foo/Bar;" — remove the leading 'L' and ';' last.
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

DexValue DexJniEnv::CallNative(DexMethod* method, const DexValue* args, size_t num_args) {
    DexValue result;
    if (method == nullptr) return result;


    // self-written libcore without native_fn; Call directly in C++.
    if (LibCoreInvoke(interpreter_, method, args, num_args, &result)) return result;
    if (method->native_fn == nullptr) return result;

    const char* shorty = nullptr;
    if (method->dex_file != nullptr) {
        shorty = method->dex_file->GetMethodShorty(
            method->dex_file->GetMethodId(method->dex_method_index));
    }
    if (shorty == nullptr) return result;

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
    // SEPARATE FP register file — they consume independent budgets, so a method
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
        // Deliberately an exception rather than a zero return. Stack-passed
        // arguments are not implemented (Apple's arm64 ABI packs them at their
        // natural size instead of uniform 8-byte slots, so guessing corrupts
        // them silently) — and silently returning 0 is the very failure mode
        // this call path was rewritten to eliminate.
        if (interpreter_ != nullptr) {
            interpreter_->ThrowException(
                "Ljava/lang/UnsatisfiedLinkError;",
                std::string("native method needs stack-passed arguments, not supported: ") +
                    (method->name != nullptr ? method->name : "?"));
        }
        last_error_ = std::string("native method needs stack-passed arguments: ") +
                      (method->name != nullptr ? method->name : "?");
        return result;
    }

    uint64_t fp_ret = 0;
    const uint64_t ret =
        kudroid_jni_call(method->native_fn, gp, ngp, fp, nfp, &fp_ret);

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
    return result;
}

DexValue DexJniEnv::CallJavaA(DexObject* receiver, DexMethod* method, const jvalue* args,
                              bool virtual_dispatch) {
    DexValue result;
    if (method == nullptr || interpreter_ == nullptr) return result;

    if (virtual_dispatch && receiver != nullptr && receiver->clazz != nullptr) {
        DexMethod* found = receiver->clazz->FindVirtualMethod(method->name, method->signature);
        if (found != nullptr) method = found;
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
                default:
                    vals.push_back(DexValue::Ref(AsObject(args[i].l)));
                    break;
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
    if (method == nullptr) return result;

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
