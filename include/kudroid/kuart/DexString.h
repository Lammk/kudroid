// Chuỗi Java: DexObject + dữ liệu UTF-8 do heap giữ.
//
// libcore biểu diễn String bằng mảng char + offset/count; ở đây chỉ cần đủ để
// truyền chuỗi giữa bytecode và native (Log.d, findViewById, tên file...), nên
// giữ UTF-8 thô cho khớp với JNI GetStringUTFChars mà không phải chuyển đổi.
#ifndef KUDROID_KUART_DEXSTRING_H
#define KUDROID_KUART_DEXSTRING_H

#include <cstdint>

#include "kudroid/kuart/DexObject.h"

namespace kudroid {
namespace kuart {

class DexString : public DexObject {
public:
    const char* utf8 = nullptr;
    uint32_t length = 0;  // số byte, không tính '\0'
};

}  // namespace kuart
}  // namespace kudroid

#endif  // KUDROID_KUART_DEXSTRING_H
