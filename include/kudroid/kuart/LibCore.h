// Hiện thực C++ cho các method native của libcore (framework/java/**).
//
// KHÔNG đi qua ABI của JNI: CallNative chỉ truyền được tối đa 6 tham số qua
// x0-x7 và không xử lý float/double (AAPCS64 dùng v0-v7). Thay vào đó
// interpreter gọi trực tiếp hàm C++ với mảng DexValue — đúng mọi kiểu, không
// cần trampoline assembly.
#ifndef KUDROID_KUART_LIBCORE_H
#define KUDROID_KUART_LIBCORE_H

#include <cstddef>

#include "kudroid/kuart/DexClass.h"
#include "kudroid/kuart/DexValue.h"

namespace kudroid {
namespace kuart {

class Interpreter;

// Gọi hiện thực libcore của `method` nếu có. Trả false nếu method không thuộc
// libcore (caller phải tự tìm symbol native của app).
bool LibCoreInvoke(Interpreter* interp, const DexMethod* method, const DexValue* args,
                   size_t num_args, DexValue* result);

// Method có hiện thực libcore hay không — dùng để không ném
// UnsatisfiedLinkError trước khi thử gọi.
bool LibCoreHasMethod(const DexMethod* method);

}  // namespace kuart
}  // namespace kudroid

#endif  // KUDROID_KUART_LIBCORE_H
