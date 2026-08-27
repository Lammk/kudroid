// C++ implementation for libcore native methods (framework/java/**).
//
// KH NG  i qua ABI c a JNI: CallNative ch  truy n  c t i  a 6 tham s  qua
// x0-x7 v  kh ng x  l  float/double (AAPCS64 d ng v0-v7). Thay v o
// interpreter g i tr c ti p h m C++ v i m ng DexValue    ng m i ki u, kh ng
// c n trampoline assembly.
#ifndef KUDROID_KUART_LIBCORE_H
#define KUDROID_KUART_LIBCORE_H

#include <cstddef>

#include "kudroid/kuart/DexClass.h"
#include "kudroid/kuart/DexValue.h"

namespace kudroid {
namespace kuart {

class Interpreter;

// G i hi n th c libcore c a `method` n u c . Tr  false n u method kh ng thu c
// libcore (caller ph i t  t m symbol native c a app).
bool LibCoreInvoke(Interpreter* interp, const DexMethod* method, const DexValue* args,
                   size_t num_args, DexValue* result);

// Method c  hi n th c libcore hay kh ng   d ng   kh ng n m
// UnsatisfiedLinkError tr c khi th  g i.
bool LibCoreHasMethod(const DexMethod* method);

}  // namespace kuart
}  // namespace kudroid

#endif  // KUDROID_KUART_LIBCORE_H
