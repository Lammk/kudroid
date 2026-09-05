// Java string: DexObject + heap-owned UTF-8 data.
// Stored as plain UTF-8 to match JNI GetStringUTFChars without conversion.
#ifndef KUDROID_KUART_DEXSTRING_H
#define KUDROID_KUART_DEXSTRING_H

#include <cstdint>

#include "kudroid/kuart/DexObject.h"

namespace kudroid {
namespace kuart {

class DexString : public DexObject {
public:
    const char* utf8 = nullptr;
    uint32_t length = 0;  // byte count, excluding '\0'

    // True when every byte is < 0x80, so byte index == char index and
    // length()/charAt() need no UTF-8 decoding. Set when the string is created.
    bool ascii = true;
};

}  // namespace kuart
}  // namespace kudroid

#endif  // KUDROID_KUART_DEXSTRING_H
