#include "kudroid/kuart/DexClass.h"

#include <cstring>

namespace kudroid {
namespace kuart {

namespace {

bool NameAndSigMatch(const DexMethod& m, const char* name, const char* signature) {
    if (std::strcmp(m.name, name) != 0) return false;
    return signature == nullptr || std::strcmp(m.signature, signature) == 0;
}

bool FieldMatch(const DexField& f, const char* name, const char* type_descriptor) {
    if (std::strcmp(f.name, name) != 0) return false;
    return type_descriptor == nullptr || std::strcmp(f.type_descriptor, type_descriptor) == 0;
}

}  // namespace

DexMethod* DexClass::FindVirtualMethod(const char* name, const char* signature) {
    for (DexClass* k = this; k != nullptr; k = k->superclass) {
        for (DexMethod& m : k->virtual_methods) {
            if (NameAndSigMatch(m, name, signature)) return &m;
        }
    }
    // Default method of interface: only found when the entire inheritance chain is absent.
    for (DexClass* k = this; k != nullptr; k = k->superclass) {
        for (DexClass* iface : k->interfaces) {
            if (iface == nullptr) continue;
            if (DexMethod* m = iface->FindVirtualMethod(name, signature)) return m;
        }
    }
    return nullptr;
}

DexMethod* DexClass::FindDirectMethod(const char* name, const char* signature) {
    for (DexClass* k = this; k != nullptr; k = k->superclass) {
        for (DexMethod& m : k->direct_methods) {
            if (NameAndSigMatch(m, name, signature)) return &m;
        }
    }
    return nullptr;
}

DexField* DexClass::FindInstanceField(const char* name, const char* type_descriptor) {
    for (DexClass* k = this; k != nullptr; k = k->superclass) {
        for (DexField& f : k->instance_fields) {
            if (FieldMatch(f, name, type_descriptor)) return &f;
        }
    }
    return nullptr;
}

DexField* DexClass::FindStaticField(const char* name, const char* type_descriptor) {
    for (DexClass* k = this; k != nullptr; k = k->superclass) {
        for (DexField& f : k->static_fields) {
            if (FieldMatch(f, name, type_descriptor)) return &f;
        }
    }
    // The static field declared in the interface is also visible from the child class.
    for (DexClass* k = this; k != nullptr; k = k->superclass) {
        for (DexClass* iface : k->interfaces) {
            if (iface == nullptr) continue;
            if (DexField* f = iface->FindStaticField(name, type_descriptor)) return f;
        }
    }
    return nullptr;
}

bool DexClass::IsSubClassOf(const DexClass* other) const {
    if (other == nullptr) return false;
    if (this == other) return true;

    // java.lang.Object is the universal superclass of all reference types and arrays.
    if (!is_primitive && other->descriptor != nullptr &&
        std::strcmp(other->descriptor, "Ljava/lang/Object;") == 0) {
        return true;
    }

    // Arrays implement Cloneable & Serializable, and are covariant for reference components.
    if (is_array) {
        if (other->descriptor != nullptr &&
            (std::strcmp(other->descriptor, "Ljava/lang/Cloneable;") == 0 ||
             std::strcmp(other->descriptor, "Ljava/io/Serializable;") == 0)) {
            return true;
        }
        if (other->is_array) {
            // Any reference array (object array or multi-dim array) is a subtype of Object[]
            if (other->descriptor != nullptr && std::strcmp(other->descriptor, "[Ljava/lang/Object;") == 0) {
                if (descriptor != nullptr && (descriptor[1] == 'L' || descriptor[1] == '[')) {
                    return true;
                }
            }
            if (component_type != nullptr && other->component_type != nullptr) {
                if (component_type->is_primitive || other->component_type->is_primitive) {
                    return component_type == other->component_type;
                }
                return component_type->IsSubClassOf(other->component_type);
            }
            if (descriptor != nullptr && other->descriptor != nullptr) {
                return std::strcmp(descriptor, other->descriptor) == 0;
            }
            return false;
        }
        return false;
    }

    for (const DexClass* k = this; k != nullptr; k = k->superclass) {
        if (k == other) return true;
        for (const DexClass* iface : k->interfaces) {
            if (iface != nullptr && iface->IsSubClassOf(other)) return true;
        }
    }
    return false;
}

std::string DexClass::PrettyName() const {
    if (descriptor == nullptr) return "<null>";
    const size_t len = std::strlen(descriptor);
    // "Lcom/foo/Bar;" -> "com.foo.Bar"
    if (len >= 2 && descriptor[0] == 'L' && descriptor[len - 1] == ';') {
        std::string out(descriptor + 1, len - 2);
        for (char& c : out) {
            if (c == '/') c = '.';
        }
        return out;
    }
    return descriptor;
}

}  // namespace kuart
}  // namespace kudroid
