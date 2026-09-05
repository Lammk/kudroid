// 64-bit value slot of interpreter — replacing ART JValue.
// DEX registers are 32-bit with wide values spanning a pair; here each slot holds a whole value.
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

static_assert(sizeof(DexValue) == 8, "DexValue ph i  ng 8 byte");

}  // namespace kuart
}  // namespace kudroid

#endif  // KUDROID_KUART_DEXVALUE_H
