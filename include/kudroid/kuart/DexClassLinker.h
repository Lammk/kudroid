// Nạp class từ DEX: resolve theo descriptor, dựng field layout + vtable.
//
// Thay cho ClassLinker của ART (~4000 LOC gắn với GC/OAT/verifier). Ở đây chỉ
// làm ba việc: tìm ClassDef trong các DEX đã mở, đọc field/method qua
// ClassAccessor, rồi link (tính offset field và gộp vtable từ superclass).
//
// KHÔNG có bytecode verifier — DEX lấy từ APK cài trực tiếp, coi là trusted.
#ifndef KUDROID_KUART_DEXCLASSLINKER_H
#define KUDROID_KUART_DEXCLASSLINKER_H

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "dex/dex_file.h"

#include "kudroid/kuart/DexClass.h"
#include "kudroid/kuart/DexClassObject.h"
#include "kudroid/kuart/DexHeap.h"
#include "kudroid/kuart/DexObject.h"
#include "kudroid/kuart/DexString.h"

namespace kudroid {
namespace kuart {

class DexClassLinker {
public:
    DexClassLinker();
    ~DexClassLinker();

    DexClassLinker(const DexClassLinker&) = delete;
    DexClassLinker& operator=(const DexClassLinker&) = delete;

    // Thêm một DEX vào classpath. Thứ tự thêm = thứ tự tìm class.
    // `bytes` phải sống lâu hơn linker (mmap của APK hoặc buffer do caller giữ).
    bool AddDexFile(const uint8_t* bytes, size_t size, const std::string& location,
                    std::string* error_msg);

    // Nạp class theo descriptor ("Lcom/foo/Bar;"). Trả nullptr nếu không tìm
    // thấy trong mọi DEX đã thêm. Kết quả được cache; gọi lại trả cùng con trỏ.
    DexClass* FindClass(const char* descriptor);

    // Tính field layout + vtable. Idempotent.
    bool LinkClass(DexClass* klass);

    DexObject* AllocObject(DexClass* klass);
    DexArray* AllocArray(DexClass* array_class, int32_t length);

    // Chuỗi cùng nội dung trả về cùng object (interning) — bytecode so sánh
    // hằng chuỗi bằng `==` vẫn đúng như trên Android thật.
    DexString* InternString(const char* utf8);

    // Chuỗi mới, KHÔNG intern — dùng cho NewStringUTF của JNI.
    DexString* NewString(const char* utf8);

    // Object java.lang.Class của `klass`, tạo lười và cache trong DexClass.
    DexClassObject* GetClassObject(DexClass* klass);

    // Chuyển object java.lang.Class về DexClass; nullptr nếu không phải.
    DexClass* ClassFromObject(DexObject* obj) const;

    // Kích thước một phần tử mảng theo component type.
    static uint32_t ElementSize(const DexClass* component);

    DexHeap& heap() { return heap_; }

    size_t NumDexFiles() const { return dex_files_.size(); }
    size_t NumLoadedClasses() const { return classes_.size(); }

    // DEX theo thứ tự đã thêm; nullptr nếu index ngoài dải.
    const art::DexFile* DexFileAt(size_t index) const {
        return index < dex_files_.size() ? dex_files_[index].get() : nullptr;
    }

    const std::string& last_error() const { return last_error_; }

private:
    DexClass* LoadClassFromDexFile(const art::DexFile& dex_file,
                                   const art::dex::ClassDef& class_def,
                                   const char* descriptor);
    DexClass* CreatePrimitiveClass(const char* descriptor, art::Primitive::Type type);
    DexClass* CreateArrayClass(const char* descriptor);

    // Kích thước một field trong object theo descriptor kiểu của nó.
    static uint32_t FieldSizeForDescriptor(const char* descriptor);

    DexHeap heap_;
    std::vector<std::unique_ptr<const art::DexFile>> dex_files_;

    // descriptor -> class. Chuỗi khoá trỏ vào DEX hoặc heap nên bền.
    std::unordered_map<std::string, DexClass*> classes_;

    // Bảng intern chuỗi hằng.
    std::unordered_map<std::string, DexString*> strings_;

    // Object java.lang.Class đã tạo. Cần để nhận diện an toàn — object guest
    // không có RTTI nên không thể cast rồi đọc field để kiểm tra.
    std::unordered_map<const DexObject*, DexClass*> class_objects_;

    // Chặn đệ quy vô hạn khi chuỗi superclass có vòng.
    std::vector<std::string> loading_;

    std::string last_error_;
};

}  // namespace kuart
}  // namespace kudroid

#endif  // KUDROID_KUART_DEXCLASSLINKER_H
