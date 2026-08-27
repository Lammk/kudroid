#include "kudroid/kuart/DexReflect.h"

#include <cstring>

#include "kudroid/kuart/DexJniEnv.h"

namespace kudroid {
namespace kuart {

std::string DexReflect::DottedToDescriptor(const char* dotted) {
    if (dotted == nullptr || dotted[0] == '\0') return std::string();

    std::string s(dotted);
    for (char& c : s) {
        if (c == '.') c = '/';
    }
    // Tên mảng đã là descriptor ("[I", "[Ljava/lang/String;").
    if (s[0] == '[') return s;
    // Java cho phép truyền cả descriptor sẵn; đừng bọc hai lần.
    if (s.size() > 2 && s.front() == 'L' && s.back() == ';') return s;
    return "L" + s + ";";
}

std::string DexReflect::DescriptorToDotted(const char* descriptor) {
    if (descriptor == nullptr || descriptor[0] == '\0') return std::string();

    std::string s(descriptor);
    // Mảng: Java trả nguyên descriptor, chỉ đổi '/' thành '.'.
    if (s[0] != '[' && s.size() > 2 && s.front() == 'L' && s.back() == ';') {
        s = s.substr(1, s.size() - 2);
    }
    for (char& c : s) {
        if (c == '/') c = '.';
    }
    return s;
}

DexClass* DexReflect::ForName(const char* dotted_name) {
    if (linker_ == nullptr || dotted_name == nullptr) return nullptr;

    const std::string descriptor = DottedToDescriptor(dotted_name);
    DexClass* klass = linker_->FindClass(descriptor.c_str());
    if (klass == nullptr) {
        last_error_ = std::string("ClassNotFoundException: ") + dotted_name;
        return nullptr;
    }
    // Class.forName mặc định initialize = true.
    if (interpreter_ != nullptr && !interpreter_->EnsureInitialized(klass)) {
        last_error_ = std::string("<clinit> thất bại: ") + dotted_name;
        return nullptr;
    }
    return klass;
}

DexObject* DexReflect::NewInstance(DexClass* klass) {
    if (klass == nullptr || linker_ == nullptr) return nullptr;
    if (klass->IsInterface() || klass->IsAbstract() || klass->is_primitive) {
        last_error_ = "InstantiationException: " + klass->PrettyName();
        return nullptr;
    }
    if (interpreter_ != nullptr && !interpreter_->EnsureInitialized(klass)) return nullptr;

    DexObject* obj = linker_->AllocObject(klass);
    if (obj == nullptr) {
        last_error_ = "OutOfMemoryError: " + klass->PrettyName();
        return nullptr;
    }

    DexMethod* ctor = klass->FindDirectMethod("<init>", "()V");
    if (ctor == nullptr) {
        last_error_ = "NoSuchMethodException: " + klass->PrettyName() + ".<init>()";
        return nullptr;
    }
    // Constructor tự khai báo nhưng không có thân xảy ra khi framework chỉ có
    // stub; coi object đã dựng xong (field đã zero) thay vì báo lỗi.
    if (ctor->code_item == nullptr && !ctor->IsNative()) return obj;

    const DexValue self = DexValue::Ref(obj);
    Invoke(ctor, obj, &self, 1);
    if (interpreter_ != nullptr && interpreter_->HasPendingException()) return nullptr;
    return obj;
}

std::string DexReflect::GetName(const DexClass* klass) const {
    if (klass == nullptr) return std::string();
    return DescriptorToDotted(klass->descriptor);
}

DexMethod* DexReflect::FindMethod(DexClass* klass, const char* name, const char* signature) {
    if (klass == nullptr || name == nullptr) return nullptr;
    if (DexMethod* m = klass->FindVirtualMethod(name, signature)) return m;
    if (DexMethod* m = klass->FindDirectMethod(name, signature)) return m;
    last_error_ = std::string("NoSuchMethodException: ") + klass->PrettyName() + "." + name +
                  (signature != nullptr ? signature : "");
    return nullptr;
}

DexValue DexReflect::Invoke(DexMethod* method, DexObject* receiver, const DexValue* args,
                            size_t num_args) {
    DexValue result;
    if (method == nullptr) return result;

    // Method.invoke dùng dispatch động: object con override thì gọi bản của con.
    if (!method->IsStatic() && receiver != nullptr && receiver->clazz != nullptr &&
        method->vtable_index != DexMethod::kInvalidVTableIndex) {
        if (DexMethod* found =
                receiver->clazz->FindVirtualMethod(method->name, method->signature)) {
            method = found;
        }
    }

    if (method->IsNative()) {
        if (jni_ == nullptr || !jni_->LinkNativeMethod(method)) {
            last_error_ = std::string("UnsatisfiedLinkError: ") + method->name;
            return result;
        }
        return jni_->CallNative(method, args, num_args);
    }
    if (interpreter_ == nullptr) return result;
    return interpreter_->Execute(method, args, num_args);
}

}  // namespace kuart
}  // namespace kudroid
