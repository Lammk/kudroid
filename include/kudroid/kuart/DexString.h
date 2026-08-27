// Java string: DexObject + UTF-8 data do heap gi .
//
// libcore bi u di n String b ng m ng char + offset/count;    y ch  c n
// truy n chu i gi a bytecode v  native (Log.d, findViewById, t n file...), n n
// gi  UTF-8 th  cho kh p v i JNI GetStringUTFChars m  kh ng ph i chuy n  i.
#ifndef KUDROID_KUART_DEXSTRING_H
#define KUDROID_KUART_DEXSTRING_H

#include <cstdint>

#include "kudroid/kuart/DexObject.h"

namespace kudroid {
namespace kuart {

class DexString : public DexObject {
public:
    const char* utf8 = nullptr;
    uint32_t length = 0;  // s  byte, kh ng t nh '\0'

    // True when every byte is < 0x80, so byte index == char index and
    // length()/charAt() need no UTF-8 decoding. Set when the string is created.
    bool ascii = true;
};

}  // namespace kuart
}  // namespace kudroid

#endif  // KUDROID_KUART_DEXSTRING_H
