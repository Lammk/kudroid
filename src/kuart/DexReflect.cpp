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
    // The array name is already a descriptor ("[I", "[Ljava/lang/String;").
    if (s[0] == '[') return s;
    // Java allows passing built-in descriptors; Don't wrap it twice.
    if (s.size() > 2 && s.front() == 'L' && s.back() == ';') return s;
    return "L" + s + ";";
}

std::string DexReflect::DescriptorToDotted(const char* descriptor) {
    if (descriptor == nullptr || descriptor[0] == '\0') return std::string();

    std::string s(descriptor);
    // Array: Java returns the descriptor, just changes '/' to '.'.
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
    // Auto-stubbed classes are placeholders with no members; treat them as absent
    // rather than returning something the caller cannot use. See DexClass::is_stub.
    if (klass == nullptr || klass->is_stub) {
        last_error_ = std::string("ClassNotFoundException: ") + dotted_name;
        return nullptr;
    }
    // Class.forName defaults to initialize = true.
    if (interpreter_ != nullptr && !interpreter_->EnsureInitialized(klass)) {
        last_error_ = std::string("<clinit> failed: ") + dotted_name;
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
    // Stubs have no members; see DexClass::is_stub.
    if (klass->is_stub) {
        last_error_ = "NoClassDefFoundError: " + klass->PrettyName();
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
    // Constructor declares itself but has no body which happens when framework only has
    // stub; Consider the object to have been constructed (field zero) instead of reporting an error.
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

    // Method.invoke uses dynamic dispatch: the override child object calls its version.
    // The receiver reaches here from JNI (CallObjectMethod on a reflected Method) as
    // well as from bytecode, so its class is validated rather than null-checked —
    // see DexClassLinker::ClassOfObject.
    if (!method->IsStatic() && receiver != nullptr && linker_ != nullptr &&
        method->vtable_index != DexMethod::kInvalidVTableIndex) {
        if (DexClass* receiver_class = linker_->ClassOfObject(receiver)) {
            if (DexMethod* found =
                    receiver_class->FindVirtualMethod(method->name, method->signature)) {
                method = found;
            }
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
