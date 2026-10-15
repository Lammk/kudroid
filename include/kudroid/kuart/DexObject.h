// Guest Java object: header + instance field data payload.
// Field offsets count from the payload start, so subclass fields append after parent fields.
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

    // Monitor for synchronized: 0 = unowned. Only touched under the monitor
    // mutex in VmLock.cpp.
    uint32_t lock_owner_tid = 0;
    uint32_t lock_count = 0;

    // Bumped by notify/notifyAll so a waiter can tell a real notification
    // from a spurious condition-variable wakeup.
    uint32_t notify_seq = 0;

    uint8_t* FieldData() { return reinterpret_cast<uint8_t*>(this) + sizeof(DexObject); }
    const uint8_t* FieldData() const {
        return reinterpret_cast<const uint8_t*>(this) + sizeof(DexObject);
    }

    // memcpy instead of cast: 8-byte fields may be misaligned; misaligned deref is UB on arm64.
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

// Java array: length + inline elements (primitives or DexObject*).
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
