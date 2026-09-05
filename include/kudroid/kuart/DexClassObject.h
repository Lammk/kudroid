// Object representing java.lang.Class for bytecode.
// Distinguishes JNI jclass (DexClass* handle) from Java Class values (real DexObject).
// Each DexClass has at most one DexClassObject, so `Foo.class == Foo.class` holds.
#ifndef KUDROID_KUART_DEXCLASSOBJECT_H
#define KUDROID_KUART_DEXCLASSOBJECT_H

#include "kudroid/kuart/DexObject.h"

namespace kudroid {
namespace kuart {

class DexClass;

class DexClassObject : public DexObject {
public:
    DexClass* represented = nullptr;
};

}  // namespace kuart
}  // namespace kudroid

#endif  // KUDROID_KUART_DEXCLASSOBJECT_H
