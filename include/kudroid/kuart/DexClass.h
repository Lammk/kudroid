// Cấu trúc C++ thay cho java.lang.Class / Field / Method của libcore.
//
// AOSP biểu diễn chúng bằng mirror::Class, mirror::ArtField, mirror::ArtMethod —
// những cấu trúc gắn chặt vào GC và object model của ART. KuDroid không port
// libcore nên dùng struct thuần, chỉ giữ đúng thứ interpreter cần: layout field,
// vtable để dispatch virtual, và con trỏ tới code_item trong DEX.
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

    // Field instance: offset byte trong phần dữ liệu của DexObject.
    // Field static: chỉ số trong mảng static_values của class.
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
    uint16_t ins_size = 0;   // số register dành cho tham số (gồm `this`)
    uint16_t outs_size = 0;

    // Số ô DexValue mà tham số chiếm; long/double tính 2 theo quy ước DEX.
    uint16_t arg_words = 0;

    // Vị trí trong vtable của class khai báo; kInvalidVTableIndex nếu
    // direct/static/constructor (không dispatch động).
    static constexpr uint16_t kInvalidVTableIndex = 0xFFFF;
    uint16_t vtable_index = kInvalidVTableIndex;

    // Hàm JNI đã liên kết cho method native (RegisterNatives hoặc dlsym).
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
        kLoaded,      // đã đọc field/method từ DEX
        kLinked,      // đã tính layout field + vtable
        kInitialized, // <clinit> đã chạy
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

    // Gộp virtual method của cả chuỗi kế thừa; override ghi đè đúng slot cha.
    std::vector<DexMethod*> vtable;

    // Giá trị field static, đánh chỉ số theo DexField::offset_or_slot.
    std::vector<DexValue> static_values;

    // Tổng byte cho field instance, gồm cả phần thừa kế từ superclass.
    uint32_t object_size = 0;

    // Class cho kiểu nguyên thủy (I, J, F...) hoặc mảng — không có class_def.
    bool is_primitive = false;
    bool is_array = false;
    art::Primitive::Type primitive_type = art::Primitive::kPrimNot;
    DexClass* component_type = nullptr;  // chỉ dùng khi is_array

    // Object java.lang.Class tương ứng, tạo lười ở lần đầu cần tới. Cache ở đây
    // để `Foo.class == Foo.class` cho ra true.
    DexObject* class_object = nullptr;

    bool IsInterface() const { return (access_flags & art::kAccInterface) != 0; }
    bool IsAbstract() const { return (access_flags & art::kAccAbstract) != 0; }

    // Tìm trong class này rồi lần lên superclass.
    DexMethod* FindVirtualMethod(const char* name, const char* signature);
    DexMethod* FindDirectMethod(const char* name, const char* signature);
    DexField* FindInstanceField(const char* name, const char* type_descriptor);
    DexField* FindStaticField(const char* name, const char* type_descriptor);

    bool IsSubClassOf(const DexClass* other) const;

    // Tên dạng Java ("com.foo.Bar") để in log/exception.
    std::string PrettyName() const;
};

}  // namespace kuart
}  // namespace kudroid

#endif  // KUDROID_KUART_DEXCLASS_H
