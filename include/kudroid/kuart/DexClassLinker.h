// Load classes from DEX: resolve by descriptor, compute field layout and vtable.
// Replaces ART ClassLinker (~4000 LOC tied to GC/OAT/verifier).
#ifndef KUDROID_KUART_DEXCLASSLINKER_H
#define KUDROID_KUART_DEXCLASSLINKER_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
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

    // True when `klass` is a class this linker created.
    //
    // Exists so the JNI layer can reject a bogus jclass instead of dereferencing
    // it. Native code hands back whatever it was given, and a wrong handle used to
    // segfault inside FindVirtualMethod with the faulting address being the bytes
    // of a string — unreadable as a diagnosis, and fatal.
    bool IsRegisteredClass(const DexClass* klass) const;

    // The class of `obj`, or nullptr when `obj` is not a usable object.
    //
    // Reading obj->clazz directly is only safe once the handle is known to be an
    // object, and native code routinely breaks that assumption in two ways:
    //
    //  - It passes a jclass where a jobject is expected. DexClass::descriptor and
    //    DexObject::clazz both sit at offset 0, so obj->clazz then yields the
    //    descriptor STRING pointer, which gets used as a class. That is the crash at
    //    0x2f657074666172eb — the ASCII bytes "raftpe/" out of
    //    "Lcom/mojang/minecraftpe/MainActivity;".
    //  - It passes a stale or fabricated handle, giving a clazz like 0x10 that is
    //    non-null and so passes an ordinary null check before faulting at a small
    //    offset (the 0x98 fault from libPlayFabMultiplayer's JNI_OnLoad).
    //
    // Both cases resolve to nullptr here, letting the caller raise a Java exception
    // that names the operation instead of taking a signal in FindVirtualMethod.
    DexClass* ClassOfObject(const DexObject* obj) const;

    // Why ClassOfObject rejected a handle.
    //
    // "invalid class pointer" alone does not say what was wrong, and the three cases
    // need different fixes: a jclass passed as a jobject is a call-site mistake, a
    // null clazz means the object was never initialised, and an unregistered pointer
    // means the handle is stale or was never ours. The 0x10 value that
    // libPlayFabMultiplayer produced could not be classified from the old message.
    enum class BadReceiver {
        kOk,             // the handle is a usable object
        kNull,           // nullptr
        kIsAClass,       // a DexClass handed in where a DexObject was expected
        kNullClass,      // an object whose clazz was never set
        kUnknownClass,   // clazz is a pointer this linker did not create
    };

    // Classify `obj` without dereferencing anything unsafe. Returns kOk exactly when
    // ClassOfObject would return non-null.
    BadReceiver ClassifyObject(const DexObject* obj) const;

    // "clazz=0x10 is not a class this runtime created" — the detail for an exception
    // message, including the offending pointer value.
    std::string DescribeBadReceiver(const DexObject* obj) const;

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

    // Absolute path for the MISSING_FRAMEWORK_CLASS log consumed by
    // scripts/generate_framework_stubs.py. Empty (the default) logs to stderr
    // only, which is what host tests want — writing a relative "classes.log"
    // used to pollute whichever directory a test was started from.
    static void SetMissingClassLogPath(const char* path);

    // Record a missing class member in the same log.
    //
    // `kind` is the tag the log line carries (MISSING_FRAMEWORK_FIELD,
    // MISSING_FRAMEWORK_METHOD). A missing field cannot be stubbed the way a
    // missing method can — object layout is fixed once LinkClass has run, so there
    // is nowhere to put the storage — which makes reporting it the only remedy. It
    // used to be reported nowhere, so the resulting NoSuchFieldError carried the
    // bare text "iput" and every occurrence cost a manual debugging round.
    static void LogMissingMember(const char* kind, const std::string& detail);

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
    // Every DexClass this linker created, for validating handles that came back
    // from native code. A set of pointers rather than a scan of classes_ because
    // GetMethodID is on the hot path of JNI-heavy startup.
    std::unordered_set<const DexClass*> live_classes_;
    std::vector<std::string> loading_;
    std::string last_error_;
};

}  // namespace kuart
}  // namespace kudroid

#endif  // KUDROID_KUART_DEXCLASSLINKER_H
