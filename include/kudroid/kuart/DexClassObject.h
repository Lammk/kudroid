// Object representing java.lang.Class cho bytecode.
//
// L U   ph n bi t hai c ch bi u di n class trong KuART:
// - `jclass` c a JNI = DexClass* cast th ng (native ch  d ng l m handle).
// - Gi  tr  Java ki u java.lang.Class = DexClassObject* (const-class,
// Object.getClass(), Class.forName())   ph i l  DexObject th t v  bytecode
// g i method tr n n  v  c  th  c t v o field/m ng.
// M i DexClass c  t i  a M T DexClassObject (cache   DexClass::class_object)
// n n `Foo.class == Foo.class` v n  ng.
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
