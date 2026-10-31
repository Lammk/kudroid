#include "kudroid/kuart/LibCore.h"
#include "kudroid/kuart/DexClassLinker.h"
#include "kudroid/kuart/DexObject.h"
#include "kudroid/kuart/Interpreter.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace kudroid::kuart {
namespace {

bool Fail(Interpreter* interp, const char* type, const char* message) {
    interp->ThrowException(type, message);
    return false;
}

size_t PrimitiveWidth(const DexObject* object) {
    if (!object || !object->clazz || !object->clazz->is_array) return 0;
    const char* desc = object->clazz->descriptor;
    if (!desc || desc[0] != '[' || desc[2] != '\0') return 0;
    switch (desc[1]) {
        case 'B': case 'Z': return 1;
        case 'C': case 'S': return 2;
        case 'I': case 'F': return 4;
        case 'J': case 'D': return 8;
        default: return 0;
    }
}

bool ArrayRegion(Interpreter* interp, DexObject* object, uint64_t offset,
                 uint64_t count, uint8_t** bytes) {
    if (!object) return Fail(interp, "Ljava/lang/NullPointerException;", "null array");
    const size_t width = PrimitiveWidth(object);
    if (!width) return Fail(interp, "Ljava/lang/IllegalArgumentException;", "expected primitive array");
    auto* array = static_cast<DexArray*>(object);
    const uint64_t size = static_cast<uint64_t>(array->length) * width;
    if (offset > size || count > size - offset)
        return Fail(interp, "Ljava/lang/ArrayIndexOutOfBoundsException;", "memory array range");
    *bytes = array->Data() + offset;
    return true;
}

bool RawRegion(Interpreter* interp, uint64_t address, uint64_t count, uint8_t** bytes) {
    if ((count && !address) || address > UINTPTR_MAX || count > UINTPTR_MAX - address)
        return Fail(interp, "Ljava/lang/IllegalArgumentException;", "invalid memory range");
    *bytes = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(address));
    return true;
}

bool ObjectRegion(Interpreter* interp, DexObject* object, int32_t offset,
                  uint64_t count, uint8_t** bytes) {
    if (!object) return Fail(interp, "Ljava/lang/NullPointerException;", "null memory object");
    if (PrimitiveWidth(object))
        return ArrayRegion(interp, object, static_cast<uint64_t>(offset), count, bytes);
    DexClass* buffer = interp->linker()->FindClass("Ljava/nio/ByteBuffer;");
    if (!object->clazz || !buffer || !object->clazz->IsSubClassOf(buffer))
        return Fail(interp, "Ljava/lang/IllegalArgumentException;", "expected array or byte buffer");
    DexField* capacity = object->clazz->FindInstanceField("capacity", "I");
    DexField* address = object->clazz->FindInstanceField("address", "J");
    if (!capacity || !address)
        return Fail(interp, "Ljava/lang/IllegalArgumentException;", "missing buffer backing");
    const int32_t size = object->GetField<int32_t>(capacity->offset_or_slot);
    if (offset < 0 || size < offset || count > static_cast<uint64_t>(size - offset))
        return Fail(interp, "Ljava/lang/IndexOutOfBoundsException;", "memory buffer range");
    const uint64_t base = object->GetField<uint64_t>(address->offset_or_slot);
    if (base) {
        if (base > UINTPTR_MAX - static_cast<uint64_t>(offset))
            return Fail(interp, "Ljava/lang/IllegalArgumentException;", "invalid buffer address");
        return RawRegion(interp, base + offset, count, bytes);
    }
    DexField* bias = object->clazz->FindInstanceField("offset", "I");
    DexObject* backing = LibCoreGetRefField(object, "hb", "[B");
    if (!bias || !backing)
        return Fail(interp, "Ljava/lang/IllegalArgumentException;", "missing buffer backing");
    const int32_t start = object->GetField<int32_t>(bias->offset_or_slot);
    return ArrayRegion(interp, backing, static_cast<uint64_t>(start) + offset, count, bytes);
}

void CopyBytes(uint8_t* dst, const uint8_t* src, size_t count, size_t width, bool swap) {
    if (!count) return;
    std::memmove(dst, src, count);
    if (swap && width > 1) {
        for (size_t i = 0; i < count; i += width)
            std::reverse(dst + i, dst + i + width);
    }
}

}  // namespace

bool Invoke_libcore_io_Memory(Interpreter* interp, const char* name, const DexValue* args,
                             size_t num_args, DexValue* result) {
    if (std::strcmp(name, "memmove") == 0 && num_args == 5) {
        uint8_t *dst, *src;
        const uint64_t count = static_cast<uint64_t>(args[4].j);
        if (ObjectRegion(interp, args[0].l, args[1].i, count, &dst) &&
            ObjectRegion(interp, args[2].l, args[3].i, count, &src) && count)
            std::memmove(dst, src, static_cast<size_t>(count));
        return true;
    }
    if ((std::strcmp(name, "unsafeBulkGet") == 0 ||
         std::strcmp(name, "unsafeBulkPut") == 0) && num_args == 7) {
        const int32_t width = args[5].i;
        const int32_t count = args[2].i;
        if ((width != 1 && width != 2 && width != 4 && width != 8) ||
            count < 0 || (args[6].i && count % width)) {
            Fail(interp, "Ljava/lang/IllegalArgumentException;", "invalid bulk memory layout");
            return true;
        }
        const bool get = std::strcmp(name, "unsafeBulkGet") == 0;
        const int32_t scaledOffset = get ? args[1].i : args[4].i;
        if (scaledOffset < 0) {
            Fail(interp, "Ljava/lang/ArrayIndexOutOfBoundsException;", "negative bulk offset");
            return true;
        }
        const uint64_t scaled = static_cast<uint64_t>(scaledOffset) * width;
        uint8_t *dst, *src;
        if (ArrayRegion(interp, args[0].l, get ? scaled : static_cast<uint64_t>(args[1].i), count, &dst) &&
            ArrayRegion(interp, args[3].l, get ? static_cast<uint64_t>(args[4].i) : scaled, count, &src))
            CopyBytes(dst, src, count, width, args[6].i != 0);
        return true;
    }

    const bool peek = std::strncmp(name, "peek", 4) == 0;
    const bool poke = std::strncmp(name, "poke", 4) == 0;
    if ((!peek && !poke) || num_args < 1) return false;
    const char* type = name + 4;
    size_t width = 0;
    char descriptor = '\0';
    const char* suffix = nullptr;
    struct Type { const char* name; size_t width; char descriptor; };
    const Type types[] = {{"Byte", 1, 'B'}, {"Char", 2, 'C'}, {"Short", 2, 'S'},
                          {"Int", 4, 'I'}, {"Long", 8, 'J'}, {"Float", 4, 'F'}, {"Double", 8, 'D'}};
    for (const Type& candidate : types) {
        const size_t length = std::strlen(candidate.name);
        if (std::strncmp(type, candidate.name, length) == 0) {
            width = candidate.width;
            descriptor = candidate.descriptor;
            suffix = type + length;
            break;
        }
    }
    if (!suffix) return false;
    if (std::strcmp(suffix, "Array") == 0) {
        if (num_args != (width == 1 ? 4u : 5u)) return false;
        if (!args[1].l || !args[1].l->clazz ||
            !args[1].l->clazz->descriptor || args[1].l->clazz->descriptor[0] != '[' ||
            args[1].l->clazz->descriptor[1] != descriptor) {
            Fail(interp, args[1].l ? "Ljava/lang/IllegalArgumentException;" : "Ljava/lang/NullPointerException;",
                 "invalid memory array type");
            return true;
        }
        uint8_t *array, *raw;
        const uint64_t count = static_cast<uint64_t>(args[3].i) * width;
        const uint64_t offset = static_cast<uint64_t>(args[2].i) * width;
        if (ArrayRegion(interp, args[1].l, offset, count, &array) &&
            RawRegion(interp, static_cast<uint64_t>(args[0].j), count, &raw)) {
            CopyBytes(peek ? array : raw, peek ? raw : array, static_cast<size_t>(count),
                      width, width != 1 && args[4].i != 0);
        }
        return true;
    }
    const bool scalar = (descriptor == 'B' && *suffix == '\0') ||
            ((descriptor == 'S' || descriptor == 'I' || descriptor == 'J') &&
             std::strcmp(suffix, "Native") == 0);
    if (!scalar || num_args != (peek ? 1u : 2u)) return false;
    uint8_t* raw;
    if (!RawRegion(interp, static_cast<uint64_t>(args[0].j), width, &raw)) return true;
    if (peek) {
        switch (descriptor) {
            case 'B': { int8_t value; std::memcpy(&value, raw, 1); *result = DexValue::Int(value); break; }
            case 'S': { int16_t value; std::memcpy(&value, raw, 2); *result = DexValue::Int(value); break; }
            case 'I': { int32_t value; std::memcpy(&value, raw, 4); *result = DexValue::Int(value); break; }
            case 'J': { int64_t value; std::memcpy(&value, raw, 8); *result = DexValue::Long(value); break; }
        }
    } else {
        if (descriptor == 'J') std::memcpy(raw, &args[1].j, width);
        else {
            // Copy low bits independently of the host's byte order.
            if (width == 1) { const int8_t value = static_cast<int8_t>(args[1].i); std::memcpy(raw, &value, 1); }
            if (width == 2) { const int16_t value = static_cast<int16_t>(args[1].i); std::memcpy(raw, &value, 2); }
            if (width == 4) std::memcpy(raw, &args[1].i, 4);
        }
    }
    return true;
}

}  // namespace kudroid::kuart
