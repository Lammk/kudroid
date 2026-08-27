// Load classes from DEX: resolve by descriptor, compute field layout and vtable.
// Replaces ART ClassLinker (~4000 LOC tied to GC/OAT/verifier).
#ifndef KUDROID_KUART_DEXCLASSLINKER_H
#define KUDROID_KUART_DEXCLASSLINKER_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "dex/dex_file.h"
#include "dex/primitive.h"

#include "kudroid/kuart/DexClass.h"
#include "kudroid/kuart/DexClassObject.h"
#include "kudroid/kuart/DexHeap.h"
#include "kudroid/kuart/DexObject.h"
#include "kudroid/kuart/DexString.h"

namespace kudroid {
namespace kuart {

class DexClassLinker {
public:
    DexClassLinker();
    ~DexClassLinker();

    // Register a DEX file for class lookup.
    bool AddDexFile(const uint8_t* bytes, size_t size, const std::string& location,
                    std::string* error_msg = nullptr);

    // Find class by descriptor ("Lcom/foo/Bar;", "[I", "I"). Returns nullptr if missing.
    DexClass* FindClass(const char* descriptor);

    // Ensure class is linked (layout and vtable resolved).
    bool LinkClass(DexClass* klass);

    // Allocate an instance of `klass`.
    DexObject* AllocObject(DexClass* klass);

    // Allocate an array of `array_class` with given length.
    DexArray* AllocArray(DexClass* array_class, int32_t length);

    // Create or get interned DexString object.
    DexString* InternString(const char* utf8);
    DexString* NewString(const char* utf8);

    // Return the java.lang.Class object representing `klass`.
    DexClassObject* GetClassObject(DexClass* klass);

    // Reverse lookup: DexClass represented by a java.lang.Class object.
    DexClass* ClassFromObject(DexObject* obj) const;

    DexHeap& heap() { return heap_; }
    const DexHeap& heap() const { return heap_; }
    const std::vector<std::unique_ptr<const art::DexFile>>& dex_files() const {
        return dex_files_;
    }
    size_t NumDexFiles() const { return dex_files_.size(); }
    const art::DexFile* DexFileAt(size_t index) const {
        return index < dex_files_.size() ? dex_files_[index].get() : nullptr;
    }
    size_t NumLoadedClasses() const { return classes_.size(); }
    size_t num_loaded_classes() const { return classes_.size(); }

    const std::string& last_error() const { return last_error_; }

    static uint32_t ElementSize(const DexClass* component);

private:
    DexClass* LoadClassFromDexFile(const art::DexFile& dex_file,
                                  const art::dex::ClassDef& class_def,
                                  const char* descriptor);
    DexClass* CreatePrimitiveClass(const char* descriptor, art::Primitive::Type type);
    DexClass* CreateArrayClass(const char* descriptor);

    static uint32_t FieldSizeForDescriptor(const char* descriptor);

    DexHeap heap_;
    std::vector<std::unique_ptr<const art::DexFile>> dex_files_;
    std::unordered_map<std::string, DexClass*> classes_;
    std::unordered_map<std::string, DexString*> strings_;
    std::unordered_map<DexObject*, DexClass*> class_objects_;
    std::vector<std::string> loading_;
    std::string last_error_;
};

}  // namespace kuart
}  // namespace kudroid

#endif  // KUDROID_KUART_DEXCLASSLINKER_H
