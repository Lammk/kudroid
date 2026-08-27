// Minimal reflection for KuART.
//
// Ch    nh ng g  ActivityThread v  code kh i  ng app d ng:
//   Class.forName(name) → newInstance() → getMethod()/invoke()
// KH NG c  annotation, generic, Field.setAccessible, Proxy, MethodHandle.
//
// Kh ng hi n th c b ng bytecode Java (kh ng c  libcore) m  b ng C++ tr c ti p:
// framework/*.java khai b o c c method n y l  `native`, DexJniEnv li n k t ch ng
// qua b ng  ng k  s n trong RegisterBuiltins().
#ifndef KUDROID_KUART_DEXREFLECT_H
#define KUDROID_KUART_DEXREFLECT_H

#include <string>

#include "kudroid/kuart/DexClassLinker.h"
#include "kudroid/kuart/Interpreter.h"

namespace kudroid {
namespace kuart {

class DexJniEnv;

class DexReflect {
public:
    DexReflect(DexClassLinker* linker, Interpreter* interpreter, DexJniEnv* jni)
        : linker_(linker), interpreter_(interpreter), jni_(jni) {}

    // Class.forName("com.foo.Bar")   nh n t n c  D U CH M nh  Java, t   i sang
    // descriptor. Ch y <clinit> nh  Java th t (initialize = true).
    DexClass* ForName(const char* dotted_name);

    // Class.newInstance(): g i constructor kh ng tham s .
    DexObject* NewInstance(DexClass* klass);

    // Class.getName() d ng "com.foo.Bar" (m ng v n gi  descriptor nh  Java).
    std::string GetName(const DexClass* klass) const;

    // Class.getMethod/getDeclaredMethod: t m theo t n + ch  k  DEX. Java th t
    // t m theo m ng Class[] parameterTypes, nh ng bytecode g i qua  y lu n bi t
    // ch  k  n n nh n th ng ch  k  cho g n v  ch nh x c h n.
    DexMethod* FindMethod(DexClass* klass, const char* name, const char* signature);

    // Method.invoke(receiver, args). `receiver` null cho method static.
    DexValue Invoke(DexMethod* method, DexObject* receiver, const DexValue* args,
                    size_t num_args);

    // i "com.foo.Bar"   "Lcom/foo/Bar;". T n m ng ("[I", "[Lcom.foo.Bar;")
    // gi  nguy n ph n '[' v  ch   i d u ch m.
    static std::string DottedToDescriptor(const char* dotted);
    static std::string DescriptorToDotted(const char* descriptor);

    const std::string& last_error() const { return last_error_; }

private:
    DexClassLinker* linker_ = nullptr;
    Interpreter* interpreter_ = nullptr;
    DexJniEnv* jni_ = nullptr;
    std::string last_error_;
};

}  // namespace kuart
}  // namespace kudroid

#endif  // KUDROID_KUART_DEXREFLECT_H
