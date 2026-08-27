// 64-bit value slot of interpreter — replacing ART JValue.
//
// DEX bytecode coi register l  32-bit; long/double chi m hai register li n k .
// y m i   l  64-bit   long/double v o g n m t  , c n opcode d ng c p
// (v0,v1) ch   c/ghi   th p. C ch n y  n gi n h n ART (d ng m ng 32-bit +
// bitmap tham chi u) v    v  KuDroid ch a c  GC c n bi t   n o l  con tr .
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
