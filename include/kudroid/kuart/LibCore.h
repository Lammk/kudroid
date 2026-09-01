// C++ implementation for libcore native methods (framework/java/**).
//
// KH NG  i qua ABI c a JNI: CallNative ch  truy n  c t i  a 6 tham s  qua
// x0-x7 v  kh ng x  l  float/double (AAPCS64 d ng v0-v7). Thay v o
// interpreter g i tr c ti p h m C++ v i m ng DexValue    ng m i ki u, kh ng
// c n trampoline assembly.
#ifndef KUDROID_KUART_LIBCORE_H
#define KUDROID_KUART_LIBCORE_H

#include <cstddef>
#include <string>
#include <vector>

#include "kudroid/kuart/DexClass.h"
#include "kudroid/kuart/DexValue.h"

namespace kudroid {
namespace kuart {

class Interpreter;
class DexClassLinker;
class DexObject;
class DexArray;

// G i hi n th c libcore c a `method` n u c . Tr  false n u method kh ng thu c
// libcore (caller ph i t  t m symbol native c a app).
bool LibCoreInvoke(Interpreter* interp, const DexMethod* method, const DexValue* args,
                   size_t num_args, DexValue* result);

// Method c  hi n th c libcore hay kh ng   d ng   kh ng n m
// UnsatisfiedLinkError tr c khi th  g i.
bool LibCoreHasMethod(const DexMethod* method);

// ── Object plumbing shared with the interpreter ───────────────────────────────
//
// These exist because proxy dispatch lives in the interpreter (it has to be
// reachable from both the bytecode invoke path and Execute()) while the boxing and
// reflection-object machinery lives here. Exposing the four helpers it needs is
// smaller than duplicating them, and keeps one definition of how a value is boxed.

// Read a reference instance field, or null when the object or field is absent.
DexObject* LibCoreGetRefField(DexObject* obj, const char* name, const char* type);

// A java.lang.reflect.Method object wrapping `m`.
DexObject* LibCoreNewMethodObject(DexClassLinker* linker, DexMethod* m);

// An Object[] holding `items`.
DexArray* LibCoreNewRefArray(DexClassLinker* linker, const char* array_descriptor,
                             const std::vector<DexObject*>& items);

// Parameter descriptors of a DEX signature: "(ILjava/lang/String;)V" -> {"I", "Ljava/lang/String;"}.
std::vector<std::string> LibCoreSplitParams(const char* signature);

// Box a primitive for a slot of type `descriptor`; reference types pass through.
DexObject* LibCoreBoxValue(Interpreter* interp, const char* descriptor, const DexValue& v);

// Inverse of LibCoreBoxValue. False when `obj` is not the expected box type.
bool LibCoreUnboxValue(Interpreter* interp, const char* descriptor, DexObject* obj,
                       DexValue* out);

using LoadLibraryCallback = int (*)(const char* libname);
void LibCoreSetLoadLibraryCallback(LoadLibraryCallback cb);

}  // namespace kuart
}  // namespace kudroid

#endif  // KUDROID_KUART_LIBCORE_H
