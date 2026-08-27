// Object đại diện java.lang.Class cho bytecode.
//
// LƯU Ý phân biệt hai cách biểu diễn class trong KuART:
//   - `jclass` của JNI = DexClass* cast thẳng (native chỉ dùng làm handle).
//   - Giá trị Java kiểu java.lang.Class = DexClassObject* (const-class,
//     Object.getClass(), Class.forName()) — phải là DexObject thật vì bytecode
//     gọi method trên nó và có thể cất vào field/mảng.
// Mỗi DexClass có tối đa MỘT DexClassObject (cache ở DexClass::class_object)
// nên `Foo.class == Foo.class` vẫn đúng.
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
