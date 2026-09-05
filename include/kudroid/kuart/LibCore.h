// C++ implementation for libcore native methods (framework/java/**).
// The interpreter calls these directly with DexValue arrays (no JNI ABI/trampoline).
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

// Run the libcore implementation of `method` if present; false means the caller
// must resolve the app's own native symbol.
bool LibCoreInvoke(Interpreter* interp, const DexMethod* method, const DexValue* args,
                   size_t num_args, DexValue* result);

// Whether `method` has a libcore implementation; used to fail early with UnsatisfiedLinkError.
bool LibCoreHasMethod(const DexMethod* method);

// Object plumbing shared with the interpreter.
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
