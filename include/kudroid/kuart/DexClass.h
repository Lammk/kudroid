// C++ structures replacing java.lang.Class / Field / Method of libcore.
//
// AOSP represents them via mirror::Class, mirror::ArtField, mirror::ArtMethod —
// structures heavily coupled to ART GC and object model. KuDroid does not port
// libcore, using pure C++ structs holding only what interpreter needs: field layout,
// vtable for virtual dispatch, and pointer to code_item in DEX.
#ifndef KUDROID_KUART_DEXCLASS_H
#define KUDROID_KUART_DEXCLASS_H

#include <cstdint>
#include <string>
#include <vector>

#include "dex/dex_file.h"
#include "dex/primitive.h"

#include "kudroid/kuart/DexValue.h"

namespace kudroid {
namespace kuart {

class DexClass;

struct DexField {
    const char* name = nullptr;
    const char* type_descriptor = nullptr;
    uint32_t access_flags = 0;
    uint32_t dex_field_index = 0;
    DexClass* declaring_class = nullptr;

    art::Primitive::Type primitive = art::Primitive::kPrimNot;

    // Instance field: byte offset within DexObject payload.
    // Static field: index within static_values array of class.
    uint32_t offset_or_slot = 0;

    bool IsStatic() const { return (access_flags & art::kAccStatic) != 0; }
};

struct DexMethod {
    const char* name = nullptr;
    const char* signature = nullptr;
    uint32_t access_flags = 0;
    uint32_t dex_method_index = 0;
    DexClass* declaring_class = nullptr;

    const art::DexFile* dex_file = nullptr;
    const art::dex::CodeItem* code_item = nullptr;

    uint16_t registers_size = 0;
    uint16_t ins_size = 0;   // number of parameter registers (including `this`)
    uint16_t outs_size = 0;

    // Number of DexValue slots occupied by parameters; long/double count as 2.
    uint16_t arg_words = 0;

    // Slot in vtable of declaring class; kInvalidVTableIndex if direct/static/ctor.
    static constexpr uint16_t kInvalidVTableIndex = 0xFFFF;
    uint16_t vtable_index = kInvalidVTableIndex;

    // Linked native function pointer (RegisterNatives or dlsym).
    void* native_fn = nullptr;

    bool IsStatic() const { return (access_flags & art::kAccStatic) != 0; }
    bool IsNative() const { return (access_flags & art::kAccNative) != 0; }
    bool IsAbstract() const { return (access_flags & art::kAccAbstract) != 0; }
    bool IsConstructor() const { return (access_flags & art::kAccConstructor) != 0; }
    bool IsDirect() const {
        return (access_flags & (art::kAccStatic | art::kAccPrivate)) != 0 || IsConstructor();
    }
};

class DexClass {
public:
    enum class Status {
        kNotLoaded,
        kLoaded,      // field/methods parsed from DEX
        kLinked,      // field layout and vtable computed
        kInitialized, // <clinit> has executed
        kError,
    };

    const char* descriptor = nullptr;  // "Lcom/foo/Bar;"
    uint32_t access_flags = 0;
    Status status = Status::kNotLoaded;

    DexClass* superclass = nullptr;
    std::vector<DexClass*> interfaces;

    const art::DexFile* dex_file = nullptr;
    const art::dex::ClassDef* class_def = nullptr;

    std::vector<DexField> static_fields;
    std::vector<DexField> instance_fields;
    std::vector<DexMethod> direct_methods;
    std::vector<DexMethod> virtual_methods;

    // Combined virtual methods of entire inheritance chain; override overwrites parent slot.
    std::vector<DexMethod*> vtable;

    // Static field values indexed by DexField::offset_or_slot.
    std::vector<DexValue> static_values;

    // Total byte size for instance fields, including inherited fields.
    uint32_t object_size = 0;

    // Primitive type (I, J, F...) or array class — no class_def.
    bool is_primitive = false;
    bool is_array = false;
    art::Primitive::Type primitive_type = art::Primitive::kPrimNot;
    DexClass* component_type = nullptr;  // used when is_array == true

    // java.lang.Class instance object, lazily created on first access.
    DexObject* class_object = nullptr;

    bool IsInterface() const { return (access_flags & art::kAccInterface) != 0; }
    bool IsAbstract() const { return (access_flags & art::kAccAbstract) != 0; }

    // Lookup within this class and traverse superclasses.
    DexMethod* FindVirtualMethod(const char* name, const char* signature);
    DexMethod* FindDirectMethod(const char* name, const char* signature);
    DexField* FindInstanceField(const char* name, const char* type_descriptor);
    DexField* FindStaticField(const char* name, const char* type_descriptor);

    bool IsSubClassOf(const DexClass* other) const;

    // Java-style name ("com.foo.Bar") for logging/exceptions.
    std::string PrettyName() const;
};

}  // namespace kuart
}  // namespace kudroid

#endif  // KUDROID_KUART_DEXCLASS_H
