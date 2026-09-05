// Minimal reflection for KuART (Class.forName/newInstance/getMethod/invoke).
// Implemented in C++ directly; framework declares these methods as native.
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

    // Class.forName("com.foo.Bar"): dotted name to descriptor; runs <clinit>.
    DexClass* ForName(const char* dotted_name);

    // Class.newInstance(): calls the no-arg constructor.
    DexObject* NewInstance(DexClass* klass);

    // Class.getName() as "com.foo.Bar".
    std::string GetName(const DexClass* klass) const;

    // Class.getMethod/getDeclaredMethod: lookup by name + DEX signature.
    DexMethod* FindMethod(DexClass* klass, const char* name, const char* signature);

    // Method.invoke(receiver, args); null receiver for static methods.
    DexValue Invoke(DexMethod* method, DexObject* receiver, const DexValue* args,
                    size_t num_args);

    // Convert "com.foo.Bar" to "Lcom/foo/Bar;"; array names keep '[' and dots.
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
