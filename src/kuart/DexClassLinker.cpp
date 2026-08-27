#include "kudroid/kuart/DexClassLinker.h"

#include <algorithm>
#include <cstring>

#include "dex/class_accessor-inl.h"
#include "dex/code_item_accessors-inl.h"
#include "dex/dex_file_loader.h"
#include "dex/modifiers.h"
#include "dex/signature.h"

namespace kudroid {
namespace kuart {

namespace {

// Đếm số ô DexValue mà danh sách tham số chiếm. Quy ước DEX: long/double chiếm
// 2 register, mọi kiểu khác chiếm 1.
uint16_t CountArgWords(const char* shorty, bool is_static) {
    uint16_t words = is_static ? 0 : 1;  // `this`
    if (shorty == nullptr) return words;
    // shorty[0] là kiểu trả về, tham số bắt đầu từ [1].
    for (const char* p = shorty + 1; *p != '\0'; ++p) {
        words += (*p == 'J' || *p == 'D') ? 2 : 1;
    }
    return words;
}

bool IsPrimitiveDescriptor(const char* d) {
    if (d == nullptr || d[0] == '\0' || d[1] != '\0') return false;
    switch (d[0]) {
        case 'Z': case 'B': case 'C': case 'S':
        case 'I': case 'J': case 'F': case 'D': case 'V':
            return true;
        default:
            return false;
    }
}

}  // namespace

DexClassLinker::DexClassLinker() = default;
DexClassLinker::~DexClassLinker() = default;

bool DexClassLinker::AddDexFile(const uint8_t* bytes, size_t size,
                                const std::string& location, std::string* error_msg) {
    const art::DexFileLoader loader;
    std::string local_error;
    std::unique_ptr<const art::DexFile> dex_file = loader.Open(
        bytes, size, location, /*location_checksum=*/0, /*oat_dex_file=*/nullptr,
        /*verify=*/false, /*verify_checksum=*/false, &local_error);
    if (dex_file == nullptr) {
        last_error_ = "AddDexFile(" + location + "): " + local_error;
        if (error_msg != nullptr) *error_msg = last_error_;
        return false;
    }
    dex_files_.push_back(std::move(dex_file));
    return true;
}

uint32_t DexClassLinker::FieldSizeForDescriptor(const char* descriptor) {
    if (descriptor == nullptr || descriptor[0] == '\0') return sizeof(void*);
    switch (descriptor[0]) {
        case 'Z': case 'B': return 1;
        case 'C': case 'S': return 2;
        case 'I': case 'F': return 4;
        case 'J': case 'D': return 8;
        default: return sizeof(DexObject*);  // L... hoặc [...
    }
}

DexClass* DexClassLinker::CreatePrimitiveClass(const char* descriptor,
                                               art::Primitive::Type type) {
    auto* klass = heap_.New<DexClass>();
    if (klass == nullptr) return nullptr;
    klass->descriptor = heap_.InternString(descriptor);
    klass->access_flags = art::kAccPublic | art::kAccFinal | art::kAccAbstract;
    klass->is_primitive = true;
    klass->primitive_type = type;
    klass->status = DexClass::Status::kInitialized;
    classes_[descriptor] = klass;
    return klass;
}

DexClass* DexClassLinker::CreateArrayClass(const char* descriptor) {
    auto* klass = heap_.New<DexClass>();
    if (klass == nullptr) return nullptr;
    klass->descriptor = heap_.InternString(descriptor);
    klass->access_flags = art::kAccPublic | art::kAccFinal;
    klass->is_array = true;
    // Đăng ký trước khi resolve component: mảng lồng ("[[I") sẽ quay lại đây.
    classes_[descriptor] = klass;

    klass->component_type = FindClass(descriptor + 1);
    klass->superclass = FindClass("Ljava/lang/Object;");
    klass->status = DexClass::Status::kLinked;
    if (klass->superclass != nullptr) {
        klass->object_size = klass->superclass->object_size;
    }
    return klass;
}

DexClass* DexClassLinker::FindClass(const char* descriptor) {
    if (descriptor == nullptr || descriptor[0] == '\0') return nullptr;

    auto it = classes_.find(descriptor);
    if (it != classes_.end()) return it->second;

    if (IsPrimitiveDescriptor(descriptor)) {
        return CreatePrimitiveClass(descriptor, art::Primitive::GetType(descriptor[0]));
    }
    if (descriptor[0] == '[') {
        return CreateArrayClass(descriptor);
    }

    // Chuỗi superclass có vòng thì dừng, nếu không sẽ đệ quy vô hạn.
    if (std::find(loading_.begin(), loading_.end(), descriptor) != loading_.end()) {
        last_error_ = std::string("vòng kế thừa khi nạp ") + descriptor;
        return nullptr;
    }

    for (const auto& dex_file : dex_files_) {
        const art::dex::TypeId* type_id = dex_file->FindTypeId(descriptor);
        if (type_id == nullptr) continue;
        const art::dex::TypeIndex type_idx = dex_file->GetIndexForTypeId(*type_id);
        const art::dex::ClassDef* class_def = dex_file->FindClassDef(type_idx);
        if (class_def == nullptr) continue;

        loading_.push_back(descriptor);
        DexClass* klass = LoadClassFromDexFile(*dex_file, *class_def, descriptor);
        loading_.pop_back();
        return klass;
    }

    last_error_ = std::string("không tìm thấy class ") + descriptor;
    return nullptr;
}

DexClass* DexClassLinker::LoadClassFromDexFile(const art::DexFile& dex_file,
                                               const art::dex::ClassDef& class_def,
                                               const char* descriptor) {
    auto* klass = heap_.New<DexClass>();
    if (klass == nullptr) return nullptr;

    klass->descriptor = heap_.InternString(descriptor);
    klass->access_flags = class_def.access_flags_;
    klass->dex_file = &dex_file;
    klass->class_def = &class_def;

    // Vào cache TRƯỚC khi resolve superclass: class tự tham chiếu chính nó qua
    // field/method sẽ tìm thấy bản đang nạp thay vì nạp lại lần nữa.
    classes_[descriptor] = klass;

    if (class_def.superclass_idx_.IsValid()) {
        const char* super_descriptor = dex_file.StringByTypeIdx(class_def.superclass_idx_);
        klass->superclass = FindClass(super_descriptor);
        if (klass->superclass == nullptr) {
            // java/lang/Object thường không có trong DEX của app — chấp nhận
            // superclass rỗng để class vẫn dùng được, thay vì fail cả class.
            last_error_ = std::string("thiếu superclass ") + super_descriptor +
                          " của " + descriptor;
        }
    }

    if (const art::dex::TypeList* ifaces = dex_file.GetInterfacesList(class_def)) {
        for (uint32_t i = 0; i < ifaces->Size(); ++i) {
            const char* iface_descriptor =
                dex_file.StringByTypeIdx(ifaces->GetTypeItem(i).type_idx_);
            klass->interfaces.push_back(FindClass(iface_descriptor));
        }
    }

    art::ClassAccessor accessor(dex_file, class_def);

    klass->static_fields.reserve(accessor.NumStaticFields());
    for (const art::ClassAccessor::Field& f : accessor.GetStaticFields()) {
        const art::dex::FieldId& field_id = dex_file.GetFieldId(f.GetIndex());
        DexField field;
        field.name = dex_file.GetFieldName(field_id);
        field.type_descriptor = dex_file.GetFieldTypeDescriptor(field_id);
        field.access_flags = f.GetAccessFlags();
        field.dex_field_index = f.GetIndex();
        field.declaring_class = klass;
        field.primitive = IsPrimitiveDescriptor(field.type_descriptor)
                              ? art::Primitive::GetType(field.type_descriptor[0])
                              : art::Primitive::kPrimNot;
        field.offset_or_slot = static_cast<uint32_t>(klass->static_fields.size());
        klass->static_fields.push_back(field);
    }
    klass->static_values.resize(klass->static_fields.size());

    klass->instance_fields.reserve(accessor.NumInstanceFields());
    for (const art::ClassAccessor::Field& f : accessor.GetInstanceFields()) {
        const art::dex::FieldId& field_id = dex_file.GetFieldId(f.GetIndex());
        DexField field;
        field.name = dex_file.GetFieldName(field_id);
        field.type_descriptor = dex_file.GetFieldTypeDescriptor(field_id);
        field.access_flags = f.GetAccessFlags();
        field.dex_field_index = f.GetIndex();
        field.declaring_class = klass;
        field.primitive = IsPrimitiveDescriptor(field.type_descriptor)
                              ? art::Primitive::GetType(field.type_descriptor[0])
                              : art::Primitive::kPrimNot;
        klass->instance_fields.push_back(field);  // offset tính ở LinkClass
    }

    const auto fill_method = [&](const art::ClassAccessor::Method& m) {
        const art::dex::MethodId& method_id = dex_file.GetMethodId(m.GetIndex());
        DexMethod method;
        method.name = dex_file.GetMethodName(method_id);
        method.signature =
            heap_.InternString(dex_file.GetMethodSignature(method_id).ToString().c_str());
        method.access_flags = m.GetAccessFlags();
        method.dex_method_index = m.GetIndex();
        method.declaring_class = klass;
        method.dex_file = &dex_file;
        method.code_item = m.GetCodeItem();
        method.arg_words = CountArgWords(dex_file.GetMethodShorty(method_id),
                                        (m.GetAccessFlags() & art::kAccStatic) != 0);
        if (method.code_item != nullptr) {
            art::CodeItemDataAccessor code(dex_file, method.code_item);
            method.registers_size = code.RegistersSize();
            method.ins_size = code.InsSize();
            method.outs_size = code.OutsSize();
        }
        return method;
    };

    klass->direct_methods.reserve(accessor.NumDirectMethods());
    for (const art::ClassAccessor::Method& m : accessor.GetDirectMethods()) {
        klass->direct_methods.push_back(fill_method(m));
    }
    klass->virtual_methods.reserve(accessor.NumVirtualMethods());
    for (const art::ClassAccessor::Method& m : accessor.GetVirtualMethods()) {
        klass->virtual_methods.push_back(fill_method(m));
    }

    klass->status = DexClass::Status::kLoaded;
    LinkClass(klass);
    return klass;
}

bool DexClassLinker::LinkClass(DexClass* klass) {
    if (klass == nullptr) return false;
    if (klass->status == DexClass::Status::kLinked ||
        klass->status == DexClass::Status::kInitialized) {
        return true;
    }

    if (klass->superclass != nullptr) {
        LinkClass(klass->superclass);
    }

    // Field instance nối tiếp field của cha, xếp field lớn trước để mỗi field
    // tự nhiên align theo kích thước của nó mà không cần chèn padding.
    uint32_t offset = klass->superclass != nullptr ? klass->superclass->object_size : 0;

    for (uint32_t want : {8u, 4u, 2u, 1u}) {
        for (DexField& f : klass->instance_fields) {
            const uint32_t size = FieldSizeForDescriptor(f.type_descriptor);
            if (size != want) continue;
            const uint32_t align = size;
            offset = (offset + align - 1) & ~(align - 1);
            f.offset_or_slot = offset;
            offset += size;
        }
    }
    klass->object_size = offset;

    // vtable: copy của cha rồi ghi đè slot khi trùng name+signature.
    klass->vtable.clear();
    if (klass->superclass != nullptr) {
        klass->vtable = klass->superclass->vtable;
    }
    for (DexMethod& m : klass->virtual_methods) {
        if (m.IsStatic() || m.IsConstructor()) continue;

        bool overridden = false;
        for (size_t i = 0; i < klass->vtable.size(); ++i) {
            DexMethod* parent = klass->vtable[i];
            if (parent == nullptr) continue;
            if (std::strcmp(parent->name, m.name) == 0 &&
                std::strcmp(parent->signature, m.signature) == 0) {
                klass->vtable[i] = &m;
                m.vtable_index = static_cast<uint16_t>(i);
                overridden = true;
                break;
            }
        }
        if (!overridden) {
            m.vtable_index = static_cast<uint16_t>(klass->vtable.size());
            klass->vtable.push_back(&m);
        }
    }

    klass->status = DexClass::Status::kLinked;
    return true;
}

DexObject* DexClassLinker::AllocObject(DexClass* klass) {
    if (klass == nullptr) return nullptr;
    if (!LinkClass(klass)) return nullptr;
    void* mem = heap_.Allocate(sizeof(DexObject) + klass->object_size);
    if (mem == nullptr) return nullptr;
    auto* obj = new (mem) DexObject();
    obj->clazz = klass;
    return obj;
}

DexArray* DexClassLinker::AllocArray(DexClass* array_class, int32_t length) {
    if (array_class == nullptr || length < 0) return nullptr;
    const uint32_t elem_size = ElementSize(array_class->component_type);
    void* mem = heap_.Allocate(sizeof(DexArray) +
                               static_cast<size_t>(length) * elem_size);
    if (mem == nullptr) return nullptr;
    auto* arr = new (mem) DexArray();
    arr->clazz = array_class;
    arr->length = length;
    return arr;
}

uint32_t DexClassLinker::ElementSize(const DexClass* component) {
    if (component == nullptr || !component->is_primitive) return sizeof(DexObject*);
    return FieldSizeForDescriptor(component->descriptor);
}

DexString* DexClassLinker::InternString(const char* utf8) {
    if (utf8 == nullptr) return nullptr;
    auto it = strings_.find(utf8);
    if (it != strings_.end()) return it->second;
    DexString* str = NewString(utf8);
    if (str != nullptr) strings_[utf8] = str;
    return str;
}

DexString* DexClassLinker::NewString(const char* utf8) {
    if (utf8 == nullptr) return nullptr;
    DexClass* string_class = FindClass("Ljava/lang/String;");
    void* mem = heap_.Allocate(sizeof(DexString));
    if (mem == nullptr) return nullptr;
    auto* str = new (mem) DexString();
    str->clazz = string_class;  // có thể null nếu framework chưa nạp
    str->utf8 = heap_.InternString(utf8);
    str->length = static_cast<uint32_t>(std::strlen(utf8));
    return str;
}

DexClassObject* DexClassLinker::GetClassObject(DexClass* klass) {
    if (klass == nullptr) return nullptr;
    if (klass->class_object != nullptr) {
        return static_cast<DexClassObject*>(klass->class_object);
    }
    void* mem = heap_.Allocate(sizeof(DexClassObject));
    if (mem == nullptr) return nullptr;
    auto* obj = new (mem) DexClassObject();
    obj->clazz = FindClass("Ljava/lang/Class;");  // null nếu framework chưa nạp
    obj->represented = klass;
    klass->class_object = obj;
    class_objects_[obj] = klass;
    return obj;
}

DexClass* DexClassLinker::ClassFromObject(DexObject* obj) const {
    if (obj == nullptr) return nullptr;
    auto it = class_objects_.find(obj);
    return it != class_objects_.end() ? it->second : nullptr;
}

}  // namespace kuart
}  // namespace kudroid
