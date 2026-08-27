// Ô giá trị 64-bit của interpreter — thay cho JValue của ART.
//
// DEX bytecode coi register là 32-bit; long/double chiếm hai register liền kề.
// Ở đây mỗi ô là 64-bit để long/double vào gọn một ô, còn opcode dạng cặp
// (v0,v1) chỉ đọc/ghi ô thấp. Cách này đơn giản hơn ART (dùng mảng 32-bit +
// bitmap tham chiếu) và đủ vì KuDroid chưa có GC cần biết ô nào là con trỏ.
#ifndef KUDROID_KUART_DEXVALUE_H
#define KUDROID_KUART_DEXVALUE_H

#include <cstdint>

namespace kudroid {
namespace kuart {

class DexObject;

union DexValue {
    int32_t i;
    int64_t j;
    float f;
    double d;
    DexObject* l;
    uint64_t raw;

    DexValue() : raw(0) {}

    static DexValue Int(int32_t v) {
        DexValue r;
        r.raw = 0;
        r.i = v;
        return r;
    }
    static DexValue Long(int64_t v) {
        DexValue r;
        r.j = v;
        return r;
    }
    static DexValue Float(float v) {
        DexValue r;
        r.raw = 0;
        r.f = v;
        return r;
    }
    static DexValue Double(double v) {
        DexValue r;
        r.d = v;
        return r;
    }
    static DexValue Ref(DexObject* v) {
        DexValue r;
        r.raw = 0;
        r.l = v;
        return r;
    }
};

static_assert(sizeof(DexValue) == 8, "DexValue phải đúng 8 byte");

}  // namespace kuart
}  // namespace kudroid

#endif  // KUDROID_KUART_DEXVALUE_H
