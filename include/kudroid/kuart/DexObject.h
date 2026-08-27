// Object của guest Java: header + phần dữ liệu field instance liền sau.
//
// Layout: [DexObject header][field bytes...]. Field offset tính từ đầu phần dữ
// liệu, nên object của subclass chỉ cần nối field mới vào sau field của cha —
// con trỏ tới subclass dùng được như con trỏ tới superclass mà không cần dịch.
#ifndef KUDROID_KUART_DEXOBJECT_H
#define KUDROID_KUART_DEXOBJECT_H

#include <cstdint>
#include <cstring>

#include "kudroid/kuart/DexValue.h"

namespace kudroid {
namespace kuart {

class DexClass;

class DexObject {
public:
    DexClass* clazz = nullptr;

    // Monitor cho synchronized: 0 = chưa ai giữ. Đủ cho mô hình đơn giản hiện
    // tại (đếm số lần vào lại + id thread giữ khoá).
    uint32_t lock_owner_tid = 0;
    uint32_t lock_count = 0;

    uint8_t* FieldData() { return reinterpret_cast<uint8_t*>(this) + sizeof(DexObject); }
    const uint8_t* FieldData() const {
        return reinterpret_cast<const uint8_t*>(this) + sizeof(DexObject);
    }

    // memcpy thay vì cast: field 8 byte không chắc align 8 trong phần dữ liệu,
    // và deref con trỏ lệch align là UB trên arm64.
    template <typename T>
    T GetField(uint32_t offset) const {
        T v;
        std::memcpy(&v, FieldData() + offset, sizeof(T));
        return v;
    }

    template <typename T>
    void SetField(uint32_t offset, T v) {
        std::memcpy(FieldData() + offset, &v, sizeof(T));
    }
};

// Mảng Java: độ dài + phần tử liền sau. Phần tử là kiểu nguyên thủy hoặc
// DexObject*, tuỳ component_type của class.
class DexArray : public DexObject {
public:
    int32_t length = 0;

    uint8_t* Data() { return reinterpret_cast<uint8_t*>(this) + sizeof(DexArray); }
    const uint8_t* Data() const {
        return reinterpret_cast<const uint8_t*>(this) + sizeof(DexArray);
    }

    template <typename T>
    T Get(int32_t index) const {
        T v;
        std::memcpy(&v, Data() + static_cast<size_t>(index) * sizeof(T), sizeof(T));
        return v;
    }

    template <typename T>
    void Set(int32_t index, T v) {
        std::memcpy(Data() + static_cast<size_t>(index) * sizeof(T), &v, sizeof(T));
    }
};

}  // namespace kuart
}  // namespace kudroid

#endif  // KUDROID_KUART_DEXOBJECT_H
