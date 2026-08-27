#include "kudroid/kuart/DexJniEnv.h"

#include <cstdlib>
#include <cstring>

#include "dex/dex_file-inl.h"
#include "dex/modifiers.h"

#include "kudroid/kuart/LibCore.h"

namespace kudroid {
namespace kuart {

namespace {

// jobject là handle mờ; KuART dùng thẳng con trỏ DexObject làm handle. Bảng
// local/global ref chỉ để quản lý thời gian sống, không để dịch địa chỉ — nên
// Decode chỉ là một cast. Đơn giản hơn ART (dùng indirect reference table) và
// đủ vì không có GC di chuyển object.
DexObject* AsObject(jobject ref) { return reinterpret_cast<DexObject*>(ref); }
jobject AsHandle(DexObject* obj) { return reinterpret_cast<jobject>(obj); }

// Mã hoá tên method theo quy ước JNI: Java_<pkg>_<Class>_<method>.
// '_' -> "_1", '/' -> '_', ';' -> "_2", '[' -> "_3".
std::string MangleJniName(const char* descriptor, const char* method_name) {
    std::string out = "Java_";
    // descriptor dạng "Lcom/foo/Bar;" — bỏ 'L' đầu và ';' cuối.
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

// Bản dài có hậu tố chữ ký, dùng khi có nhiều overload cùng tên.
std::string MangleJniNameLong(const char* descriptor, const char* method_name,
                             const char* signature) {
    std::string out = MangleJniName(descriptor, method_name);
    out += "__";
    // Chỉ phần tham số, bỏ kiểu trả về.
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
    // Frame ngoài cùng luôn phải còn để AddLocalRef không cần kiểm tra rỗng.
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
            last_error_ = std::string("RegisterNatives: không có method ") + m.name +
                          m.signature + " trong " + klass->PrettyName();
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
    // Thử tên ngắn trước (không hậu tố chữ ký) như linker JNI của Android.
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
    last_error_ = "không tìm thấy symbol native: " + short_name;
    return false;
}

DexValue DexJniEnv::CallNative(DexMethod* method, const DexValue* args, size_t num_args) {
    DexValue result;
    if (method == nullptr) return result;

    // libcore tự viết không có native_fn; gọi thẳng bằng C++.
    if (LibCoreInvoke(interpreter_, method, args, num_args, &result)) return result;
    if (method->native_fn == nullptr) return result;

    const char* shorty = nullptr;
    if (method->dex_file != nullptr) {
        shorty = method->dex_file->GetMethodShorty(
            method->dex_file->GetMethodId(method->dex_method_index));
    }
    if (shorty == nullptr) return result;

    // ABI của JNI: (JNIEnv*, jclass|jobject, ...tham số). Chỉ hỗ trợ tới 6
    // tham số vì đó là số register truyền tham số của AAPCS64 — quá số này
    // phải đẩy stack, cần assembly riêng.
    const size_t kMaxJniArgs = 6;
    if (num_args > kMaxJniArgs) {
        last_error_ = std::string("method native quá nhiều tham số: ") + method->name;
        return result;
    }

    // Tham số đầu sau env là receiver (instance) hoặc jclass (static).
    void* self;
    size_t first = 0;
    if (method->IsStatic()) {
        self = reinterpret_cast<void*>(method->declaring_class);
    } else {
        self = num_args > 0 ? reinterpret_cast<void*>(args[0].l) : nullptr;
        first = 1;
    }

    // Ép sang uintptr_t để mọi kiểu đi qua cùng một chữ ký hàm; float/double
    // cần đường riêng vì AAPCS64 truyền chúng qua register v0-v7.
    uintptr_t raw[kMaxJniArgs] = {0};
    bool has_float = false;
    size_t slot = 0;
    if (shorty[0] != '\0') {
        size_t arg_index = first;
        for (const char* p = shorty + 1; *p != '\0' && arg_index < num_args; ++p, ++arg_index) {
            switch (*p) {
                case 'F': case 'D':
                    has_float = true;
                    std::memcpy(&raw[slot], &args[arg_index].raw, sizeof(uintptr_t));
                    break;
                case 'J':
                    raw[slot] = static_cast<uintptr_t>(args[arg_index].j);
                    break;
                case 'L': case '[':
                    raw[slot] = reinterpret_cast<uintptr_t>(args[arg_index].l);
                    break;
                default:
                    raw[slot] = static_cast<uintptr_t>(
                        static_cast<uint32_t>(args[arg_index].i));
                    break;
            }
            ++slot;
        }
    }

    if (has_float) {
        last_error_ = std::string("method native có tham số float/double chưa hỗ trợ: ") +
                      method->name;
        return result;
    }

    JNIEnv* e = env();
    using Fn0 = uintptr_t (*)(JNIEnv*, void*);
    using Fn1 = uintptr_t (*)(JNIEnv*, void*, uintptr_t);
    using Fn2 = uintptr_t (*)(JNIEnv*, void*, uintptr_t, uintptr_t);
    using Fn3 = uintptr_t (*)(JNIEnv*, void*, uintptr_t, uintptr_t, uintptr_t);
    using Fn4 = uintptr_t (*)(JNIEnv*, void*, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
    using Fn5 = uintptr_t (*)(JNIEnv*, void*, uintptr_t, uintptr_t, uintptr_t, uintptr_t,
                              uintptr_t);
    using Fn6 = uintptr_t (*)(JNIEnv*, void*, uintptr_t, uintptr_t, uintptr_t, uintptr_t,
                              uintptr_t, uintptr_t);

    uintptr_t ret = 0;
    void* fn = method->native_fn;
    switch (slot) {
        case 0: ret = reinterpret_cast<Fn0>(fn)(e, self); break;
        case 1: ret = reinterpret_cast<Fn1>(fn)(e, self, raw[0]); break;
        case 2: ret = reinterpret_cast<Fn2>(fn)(e, self, raw[0], raw[1]); break;
        case 3: ret = reinterpret_cast<Fn3>(fn)(e, self, raw[0], raw[1], raw[2]); break;
        case 4: ret = reinterpret_cast<Fn4>(fn)(e, self, raw[0], raw[1], raw[2], raw[3]); break;
        case 5:
            ret = reinterpret_cast<Fn5>(fn)(e, self, raw[0], raw[1], raw[2], raw[3], raw[4]);
            break;
        default:
            ret = reinterpret_cast<Fn6>(fn)(e, self, raw[0], raw[1], raw[2], raw[3], raw[4],
                                            raw[5]);
            break;
    }

    switch (shorty[0]) {
        case 'V': break;
        case 'Z': result = DexValue::Int(ret != 0 ? 1 : 0); break;
        case 'B': result = DexValue::Int(static_cast<int8_t>(ret)); break;
        case 'C': result = DexValue::Int(static_cast<uint16_t>(ret)); break;
        case 'S': result = DexValue::Int(static_cast<int16_t>(ret)); break;
        case 'I': result = DexValue::Int(static_cast<int32_t>(ret)); break;
        case 'J': result = DexValue::Long(static_cast<int64_t>(ret)); break;
        case 'L': case '[':
            result = DexValue::Ref(reinterpret_cast<DexObject*>(ret));
            break;
        default:
            // float/double trả về nằm ở register v0, không phải x0 — chưa lấy được.
            last_error_ = std::string("kiểu trả về float/double chưa hỗ trợ: ") + method->name;
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

    // jvalue là union 64-bit nên long/double vẫn chỉ chiếm MỘT phần tử args,
    // khớp quy ước DexValue của interpreter.
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

    // va_arg promote: mọi kiểu nhỏ hơn int lên int, float lên double.
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
