#include "kudroid/kuart/DexClassLinker.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <mutex>
#include <set>
#include <string>

#include "dex/class_accessor-inl.h"
#include "dex/code_item_accessors-inl.h"
#include "dex/dex_file_loader.h"
#include "dex/modifiers.h"
#include "dex/signature.h"

namespace kudroid {
namespace kuart {

namespace {

// Count the number of DexValue cells the parameter list occupies. DEX convention: long/double accounts
// 2 registers, all other types take 1.
uint16_t CountArgWords(const char* shorty, bool is_static) {
    uint16_t words = is_static ? 0 : 1;  // `this`
    if (shorty == nullptr) return words;
    // shorty[0] is the return type, parameters start from [1].
    for (const char* p = shorty + 1; *p != '\0'; ++p) {
        words += (*p == 'J' || *p == 'D') ? 2 : 1;
    }
    return words;
}

bool IsPrimitiveDescriptor(const char* d) {
    if (d == nullptr || d[0] == '\0' || d[1] != '\0') return false;
    switch (d[0]) {
        case 'Z': case 'B': case 'C': case 'S':
        case 'I': case 'J': case 'F': case 'D': case 'V':
            return true;
        default:
            return false;
    }
}

// Where to record classes KuDroid failed to resolve. Set by KuArtRuntime to the
// app's writable directory; scripts/generate_framework_stubs.py reads the file to
// generate stubs.
//
// Absolute path on purpose. This used to open "classes.log" relative to the CWD,
// which meant every host test run littered whatever directory it happened to be
// started from — the stale classes.log committed at the repo root came from
// kuart-tests binaries, not from a real APK launch, which made it look like
// java.lang.String was missing at runtime when it is present in framework.dex.
std::mutex g_missing_class_log_mtx;
std::vector<std::string> g_missing_class_log_paths;

// Append one "[timestamp] KIND: detail" line to every configured log.
//
// Shared by the missing-class, missing-field and missing-method paths so all three
// land in the same file with the same shape; scripts/generate_framework_stubs.py
// parses it. A missing field used to be reported nowhere at all — the interpreter
// threw NoSuchFieldError with the bare text "iput" — so each one cost a manual
// debugging round.
void AppendMissingLog(const char* kind, const std::string& detail) {
    std::vector<std::string> paths;
    {
        std::lock_guard<std::mutex> lock(g_missing_class_log_mtx);
        paths = g_missing_class_log_paths;
    }
    // No path configured (host tests, or before kuart_init) -> log to stderr only.
    if (paths.empty()) return;

    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    char timeBuf[64] = {0};
    std::tm tm_buf{};
#if defined(_WIN32)
    localtime_s(&tm_buf, &now);
#else
    localtime_r(&now, &tm_buf);
#endif
    std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", &tm_buf);

    for (const auto& path : paths) {
        if (path.empty()) continue;
        std::ofstream logFile(path, std::ios::app);
        if (logFile.is_open()) {
            logFile << "[" << timeBuf << "] " << kind << ": " << detail << "\n";
            logFile.flush();
        }
    }
}

void AppendMissingClassLog(const std::string& dotted, const char* descriptor) {
    AppendMissingLog("MISSING_FRAMEWORK_CLASS",
                     dotted + " (descriptor: " + descriptor + ")");
}

}  // namespace

void DexClassLinker::LogMissingMember(const char* kind, const std::string& detail) {
    AppendMissingLog(kind, detail);
}

void DexClassLinker::SetMissingClassLogPath(const char* path) {
    std::lock_guard<std::mutex> lock(g_missing_class_log_mtx);
    if (path == nullptr || path[0] == '\0') {
        g_missing_class_log_paths.clear();
        return;
    }
    std::string s(path);
    if (std::find(g_missing_class_log_paths.begin(), g_missing_class_log_paths.end(), s) == g_missing_class_log_paths.end()) {
        g_missing_class_log_paths.push_back(s);
    }
}

DexClassLinker::DexClassLinker() = default;
DexClassLinker::~DexClassLinker() = default;

bool DexClassLinker::AddDexFile(const uint8_t* bytes, size_t size,
                                const std::string& location, std::string* error_msg) {
    const art::DexFileLoader loader;
    std::string local_error;
    std::unique_ptr<const art::DexFile> dex_file = loader.Open(
        bytes, size, location, /*location_checksum=*/0, /*oat_dex_file=*/nullptr,
        /*verify=*/false, /*verify_checksum=*/false, &local_error);
    if (dex_file == nullptr) {
        last_error_ = "AddDexFile(" + location + "): " + local_error;
        if (error_msg != nullptr) *error_msg = last_error_;
        return false;
    }
    dex_files_.push_back(std::move(dex_file));
    return true;
}

uint32_t DexClassLinker::FieldSizeForDescriptor(const char* descriptor) {
    if (descriptor == nullptr || descriptor[0] == '\0') return sizeof(void*);
    switch (descriptor[0]) {
        case 'Z': case 'B': return 1;
        case 'C': case 'S': return 2;
        case 'I': case 'F': return 4;
        case 'J': case 'D': return 8;
        default: return sizeof(DexObject*);  // L... or [...
    }
}

DexClass* DexClassLinker::CreatePrimitiveClass(const char* descriptor,
                                               art::Primitive::Type type) {
    auto* klass = heap_.New<DexClass>();
    if (klass == nullptr) return nullptr;
    klass->descriptor = heap_.InternString(descriptor);
    klass->access_flags = art::kAccPublic | art::kAccFinal | art::kAccAbstract;
    klass->is_primitive = true;
    klass->primitive_type = type;
    klass->status = DexClass::Status::kInitialized;
    classes_[descriptor] = klass;
    live_classes_.insert(klass);
    return klass;
}

DexClass* DexClassLinker::CreateArrayClass(const char* descriptor) {
    auto* klass = heap_.New<DexClass>();
    if (klass == nullptr) return nullptr;
    klass->descriptor = heap_.InternString(descriptor);
    klass->access_flags = art::kAccPublic | art::kAccFinal;
    klass->is_array = true;
    // Register before resolving the component: the nested array ("[[I") will return here.
    classes_[descriptor] = klass;
    live_classes_.insert(klass);

    klass->component_type = FindClass(descriptor + 1);
    klass->superclass = FindClass("Ljava/lang/Object;");
    klass->status = DexClass::Status::kLinked;
    if (klass->superclass != nullptr) {
        klass->object_size = klass->superclass->object_size;
    }
    return klass;
}

DexClass* DexClassLinker::FindClass(const char* descriptor) {
    if (descriptor == nullptr || descriptor[0] == '\0') return nullptr;

    auto it = classes_.find(descriptor);
    if (it != classes_.end()) return it->second;

    if (IsPrimitiveDescriptor(descriptor)) {
        return CreatePrimitiveClass(descriptor, art::Primitive::GetType(descriptor[0]));
    }
    if (descriptor[0] == '[') {
        return CreateArrayClass(descriptor);
    }

    // If the superclass chain has a loop, it stops, otherwise it will recur indefinitely.
    if (std::find(loading_.begin(), loading_.end(), descriptor) != loading_.end()) {
        last_error_ = std::string("inherit loop when loading ") + descriptor;
        return nullptr;
    }

    for (const auto& dex_file : dex_files_) {
        const art::dex::TypeId* type_id = dex_file->FindTypeId(descriptor);
        if (type_id == nullptr) continue;
        const art::dex::TypeIndex type_idx = dex_file->GetIndexForTypeId(*type_id);
        const art::dex::ClassDef* class_def = dex_file->FindClassDef(type_idx);
        if (class_def == nullptr) continue;

        loading_.push_back(descriptor);
        DexClass* klass = LoadClassFromDexFile(*dex_file, *class_def, descriptor);
        loading_.pop_back();
        return klass;
    }

    // ── Auto-stubbing for missing BOOT-CLASSPATH classes ──
    //
    // Only packages KuDroid is responsible for shipping are stubbed. An app's own
    // packages must NOT be: a missing app class means either the candidate name is
    // wrong or a DEX is missing, and both cases have to fail so the caller can
    // react. ActivityThread.handleLaunchActivity guesses names like
    // "<pkg>.Main"/"<pkg>.MainActivity" and walks to the next candidate when
    // Class.forName throws — stubbing those guesses made forName "succeed" for a
    // class that does not exist, so the cast to Activity blew up with a
    // ClassCastException and the remaining candidates were never tried.
    auto isBootClasspathDescriptor = [](const char* desc) -> bool {
        if (!desc || desc[0] != 'L') return false;
        return (std::strncmp(desc, "Landroid/", 9) == 0 ||
                std::strncmp(desc, "Landroidx/", 10) == 0 ||
                std::strncmp(desc, "Ljava/", 6) == 0 ||
                std::strncmp(desc, "Ljavax/", 7) == 0 ||
                std::strncmp(desc, "Ldalvik/", 8) == 0 ||
                std::strncmp(desc, "Lsun/", 5) == 0 ||
                std::strncmp(desc, "Llibcore/", 9) == 0 ||
                std::strncmp(desc, "Lcom/android/", 13) == 0 ||
                std::strncmp(desc, "Lorg/apache/harmony/", 20) == 0 ||
                std::strncmp(desc, "Lorg/w3c/dom/", 13) == 0 ||
                std::strncmp(desc, "Lorg/xml/sax/", 13) == 0 ||
                std::strncmp(desc, "Lorg/xmlpull/", 13) == 0 ||
                std::strncmp(desc, "Lorg/json/", 10) == 0);
    };

    const size_t descLen = std::strlen(descriptor);
    if (isBootClasspathDescriptor(descriptor) && descLen > 2 && descriptor[descLen - 1] == ';') {
        static std::mutex s_log_mtx;
        static std::set<std::string> s_logged_classes;
        {
            std::lock_guard<std::mutex> lock(s_log_mtx);
            if (s_logged_classes.insert(descriptor).second) {
                std::string dotted = descriptor;
                dotted = dotted.substr(1, dotted.size() - 2);
                std::replace(dotted.begin(), dotted.end(), '/', '.');

                std::fprintf(stderr, "[KuART][AUTO-STUB] ⚠ Auto-stubbed missing framework class: %s (%s) -> logged to classes.log\n",
                             dotted.c_str(), descriptor);
                AppendMissingClassLog(dotted, descriptor);
            }
        }

        auto* stub = heap_.New<DexClass>();
        if (stub != nullptr) {
            stub->descriptor = heap_.InternString(descriptor);
            stub->access_flags = art::kAccPublic;
            // Marked initialized so that resolving a reference to it does not try
            // to run a <clinit> it does not have, but flagged as a stub so that
            // Class.forName and new-instance refuse it. See DexClass::is_stub.
            stub->status = DexClass::Status::kInitialized;
            stub->is_stub = true;
            classes_[descriptor] = stub;
            live_classes_.insert(stub);

            stub->superclass = FindClass("Ljava/lang/Object;");
            if (stub->superclass != nullptr) {
                stub->object_size = stub->superclass->object_size;
            }
            return stub;
        }
    }

    last_error_ = std::string("class not found") + descriptor;
    return nullptr;
}

DexClass* DexClassLinker::LoadClassFromDexFile(const art::DexFile& dex_file,
                                               const art::dex::ClassDef& class_def,
                                               const char* descriptor) {
    auto* klass = heap_.New<DexClass>();
    if (klass == nullptr) return nullptr;

    klass->descriptor = heap_.InternString(descriptor);
    klass->access_flags = class_def.access_flags_;
    klass->dex_file = &dex_file;
    klass->class_def = &class_def;

    // Go to cache BEFORE resolving the superclass: the class references itself
    // field/method will find the currently loaded version instead of reloading it again.
    classes_[descriptor] = klass;
    live_classes_.insert(klass);

    if (class_def.superclass_idx_.IsValid()) {
        const char* super_descriptor = dex_file.StringByTypeIdx(class_def.superclass_idx_);
        klass->superclass = FindClass(super_descriptor);
        if (klass->superclass == nullptr) {
            // java/lang/Object is usually not included in the app's DEX — accepted
            // The superclass is empty so the class can still be used, instead of failing the entire class.
            last_error_ = std::string("missing superclass ") + super_descriptor +
                          " of " + descriptor;
        }
    }

    if (const art::dex::TypeList* ifaces = dex_file.GetInterfacesList(class_def)) {
        for (uint32_t i = 0; i < ifaces->Size(); ++i) {
            const char* iface_descriptor =
                dex_file.StringByTypeIdx(ifaces->GetTypeItem(i).type_idx_);
            klass->interfaces.push_back(FindClass(iface_descriptor));
        }
    }

    art::ClassAccessor accessor(dex_file, class_def);

    klass->static_fields.reserve(accessor.NumStaticFields());
    for (const art::ClassAccessor::Field& f : accessor.GetStaticFields()) {
        const art::dex::FieldId& field_id = dex_file.GetFieldId(f.GetIndex());
        DexField field;
        field.name = dex_file.GetFieldName(field_id);
        field.type_descriptor = dex_file.GetFieldTypeDescriptor(field_id);
        field.access_flags = f.GetAccessFlags();
        field.dex_field_index = f.GetIndex();
        field.declaring_class = klass;
        field.primitive = IsPrimitiveDescriptor(field.type_descriptor)
                              ? art::Primitive::GetType(field.type_descriptor[0])
                              : art::Primitive::kPrimNot;
        field.offset_or_slot = static_cast<uint32_t>(klass->static_fields.size());
        klass->static_fields.push_back(field);
    }
    klass->static_values.resize(klass->static_fields.size());

    // Constant initialisers for static fields.
    //
    // d8 does NOT emit a <clinit> for `static final int X = -1`; it puts the value in
    // the class_def's static_values array instead, and the runtime is expected to
    // apply it. KuDroid only zero-filled, so every compile-time constant read as 0.
    // That is silent and wrong rather than a crash: ViewGroup.LayoutParams.
    // MATCH_PARENT came back 0 instead of -1, so a view asking to fill its parent got
    // a zero-width layout and drew nothing.
    //
    // The array is positional — the Nth value initialises the Nth static field — and
    // may be shorter than the field list, in which case the rest stay zero.
    if (class_def.static_values_off_ != 0) {
        art::EncodedStaticFieldValueIterator it(dex_file, class_def);
        for (size_t slot = 0; it.HasNext() && slot < klass->static_values.size();
             it.Next(), ++slot) {
            const jvalue& v = it.GetJavaValue();
            switch (it.GetValueType()) {
                case art::EncodedArrayValueIterator::kBoolean:
                    klass->static_values[slot] = DexValue::Int(v.z != 0 ? 1 : 0);
                    break;
                case art::EncodedArrayValueIterator::kByte:
                    klass->static_values[slot] = DexValue::Int(v.b);
                    break;
                case art::EncodedArrayValueIterator::kShort:
                    klass->static_values[slot] = DexValue::Int(v.s);
                    break;
                case art::EncodedArrayValueIterator::kChar:
                    klass->static_values[slot] = DexValue::Int(v.c);
                    break;
                case art::EncodedArrayValueIterator::kInt:
                    klass->static_values[slot] = DexValue::Int(v.i);
                    break;
                case art::EncodedArrayValueIterator::kLong:
                    klass->static_values[slot] = DexValue::Long(v.j);
                    break;
                case art::EncodedArrayValueIterator::kFloat:
                    klass->static_values[slot] = DexValue::Float(v.f);
                    break;
                case art::EncodedArrayValueIterator::kDouble:
                    klass->static_values[slot] = DexValue::Double(v.d);
                    break;
                case art::EncodedArrayValueIterator::kString: {
                    // The iterator hands back a string index, not a pointer.
                    const art::dex::StringIndex idx(static_cast<uint32_t>(v.i));
                    const char* utf8 = dex_file.StringDataByIdx(idx);
                    klass->static_values[slot] =
                        DexValue::Ref(reinterpret_cast<DexObject*>(NewString(utf8)));
                    break;
                }
                case art::EncodedArrayValueIterator::kNull:
                    klass->static_values[slot] = DexValue::Ref(nullptr);
                    break;
                default:
                    // kType/kEnum/kField/kMethod/kArray/kAnnotation: these need a
                    // resolved object, and resolving one here would recurse into a
                    // class that is still being loaded. <clinit> assigns them anyway
                    // for every case KuDroid runs, so leaving the slot zero is
                    // correct rather than merely convenient.
                    break;
            }
        }
    }

    klass->instance_fields.reserve(accessor.NumInstanceFields());
    for (const art::ClassAccessor::Field& f : accessor.GetInstanceFields()) {
        const art::dex::FieldId& field_id = dex_file.GetFieldId(f.GetIndex());
        DexField field;
        field.name = dex_file.GetFieldName(field_id);
        field.type_descriptor = dex_file.GetFieldTypeDescriptor(field_id);
        field.access_flags = f.GetAccessFlags();
        field.dex_field_index = f.GetIndex();
        field.declaring_class = klass;
        field.primitive = IsPrimitiveDescriptor(field.type_descriptor)
                              ? art::Primitive::GetType(field.type_descriptor[0])
                              : art::Primitive::kPrimNot;
        klass->instance_fields.push_back(field);  // offset calculated at LinkClass
    }

    const auto fill_method = [&](const art::ClassAccessor::Method& m) {
        const art::dex::MethodId& method_id = dex_file.GetMethodId(m.GetIndex());
        DexMethod method;
        method.name = dex_file.GetMethodName(method_id);
        method.signature =
            heap_.InternString(dex_file.GetMethodSignature(method_id).ToString().c_str());
        method.access_flags = m.GetAccessFlags();
        method.dex_method_index = m.GetIndex();
        method.declaring_class = klass;
        method.dex_file = &dex_file;
        method.code_item = m.GetCodeItem();
        method.arg_words = CountArgWords(dex_file.GetMethodShorty(method_id),
                                        (m.GetAccessFlags() & art::kAccStatic) != 0);
        if (method.code_item != nullptr) {
            art::CodeItemDataAccessor code(dex_file, method.code_item);
            method.registers_size = code.RegistersSize();
            method.ins_size = code.InsSize();
            method.outs_size = code.OutsSize();
        }
        return method;
    };

    klass->direct_methods.reserve(accessor.NumDirectMethods());
    for (const art::ClassAccessor::Method& m : accessor.GetDirectMethods()) {
        klass->direct_methods.push_back(fill_method(m));
    }
    klass->virtual_methods.reserve(accessor.NumVirtualMethods());
    for (const art::ClassAccessor::Method& m : accessor.GetVirtualMethods()) {
        klass->virtual_methods.push_back(fill_method(m));
    }

    klass->status = DexClass::Status::kLoaded;
    LinkClass(klass);
    return klass;
}

bool DexClassLinker::LinkClass(DexClass* klass) {
    if (klass == nullptr) return false;
    if (klass->status == DexClass::Status::kLinked ||
        klass->status == DexClass::Status::kInitialized) {
        return true;
    }

    if (klass->superclass != nullptr) {
        LinkClass(klass->superclass);
    }

    // The field instance follows the parent field, placing the larger field first for each field
    // naturally align to its size without adding padding.
    uint32_t offset = klass->superclass != nullptr ? klass->superclass->object_size : 0;

    for (uint32_t want : {8u, 4u, 2u, 1u}) {
        for (DexField& f : klass->instance_fields) {
            const uint32_t size = FieldSizeForDescriptor(f.type_descriptor);
            if (size != want) continue;
            const uint32_t align = size;
            offset = (offset + align - 1) & ~(align - 1);
            f.offset_or_slot = offset;
            offset += size;
        }
    }
    klass->object_size = offset;

    // vtable: copy from parent and overwrite slot when name+signature overlaps.
    klass->vtable.clear();
    if (klass->superclass != nullptr) {
        klass->vtable = klass->superclass->vtable;
    }
    for (DexMethod& m : klass->virtual_methods) {
        if (m.IsStatic() || m.IsConstructor()) continue;

        bool overridden = false;
        for (size_t i = 0; i < klass->vtable.size(); ++i) {
            DexMethod* parent = klass->vtable[i];
            if (parent == nullptr) continue;
            if (std::strcmp(parent->name, m.name) == 0 &&
                std::strcmp(parent->signature, m.signature) == 0) {
                klass->vtable[i] = &m;
                m.vtable_index = static_cast<uint16_t>(i);
                overridden = true;
                break;
            }
        }
        if (!overridden) {
            m.vtable_index = static_cast<uint16_t>(klass->vtable.size());
            klass->vtable.push_back(&m);
        }
    }

    klass->status = DexClass::Status::kLinked;
    return true;
}

DexObject* DexClassLinker::AllocObject(DexClass* klass) {
    if (klass == nullptr) return nullptr;
    if (!LinkClass(klass)) return nullptr;

    // String and Class instances carry a native payload (DexString::utf8,
    // DexClassObject::represented) laid out right after the DexObject header.
    // Their Java classes declare no instance fields, so object_size is 0 and a
    // plain allocation would be too small for the native code to write into.
    size_t bytes = sizeof(DexObject) + klass->object_size;
    if (klass->descriptor != nullptr) {
        if (std::strcmp(klass->descriptor, "Ljava/lang/String;") == 0) {
            bytes = std::max(bytes, sizeof(DexString));
        } else if (std::strcmp(klass->descriptor, "Ljava/lang/Class;") == 0) {
            bytes = std::max(bytes, sizeof(DexClassObject));
        }
    }

    void* mem = heap_.Allocate(bytes);
    if (mem == nullptr) return nullptr;
    auto* obj = new (mem) DexObject();
    obj->clazz = klass;
    return obj;
}

DexArray* DexClassLinker::AllocArray(DexClass* array_class, int32_t length) {
    if (array_class == nullptr || length < 0) return nullptr;
    const uint32_t elem_size = ElementSize(array_class->component_type);
    void* mem = heap_.Allocate(sizeof(DexArray) +
                               static_cast<size_t>(length) * elem_size);
    if (mem == nullptr) return nullptr;
    auto* arr = new (mem) DexArray();
    arr->clazz = array_class;
    arr->length = length;
    return arr;
}

uint32_t DexClassLinker::ElementSize(const DexClass* component) {
    if (component == nullptr || !component->is_primitive) return sizeof(DexObject*);
    return FieldSizeForDescriptor(component->descriptor);
}

DexString* DexClassLinker::InternString(const char* utf8) {
    if (utf8 == nullptr) return nullptr;
    auto it = strings_.find(utf8);
    if (it != strings_.end()) return it->second;
    DexString* str = NewString(utf8);
    if (str != nullptr) strings_[utf8] = str;
    return str;
}

DexString* DexClassLinker::NewString(const char* utf8) {
    if (utf8 == nullptr) return nullptr;
    DexClass* string_class = FindClass("Ljava/lang/String;");
    void* mem = heap_.Allocate(sizeof(DexString));
    if (mem == nullptr) return nullptr;
    auto* str = new (mem) DexString();
    str->clazz = string_class;  // can be null if the framework is not loaded
    str->utf8 = heap_.InternString(utf8);
    str->length = static_cast<uint32_t>(std::strlen(utf8));    bool ascii = true;
    for (const char* p = utf8; *p != '\0'; ++p) {
        if (static_cast<unsigned char>(*p) >= 0x80) {
            ascii = false;
            break;
        }
    }
    str->ascii = ascii;    return str;
}

DexClassObject* DexClassLinker::GetClassObject(DexClass* klass) {
    if (klass == nullptr) return nullptr;
    if (klass->class_object != nullptr) {
        return static_cast<DexClassObject*>(klass->class_object);
    }
    void* mem = heap_.Allocate(sizeof(DexClassObject));
    if (mem == nullptr) return nullptr;
    auto* obj = new (mem) DexClassObject();
    obj->clazz = FindClass("Ljava/lang/Class;");  // null if the framework is not loaded
    obj->represented = klass;
    klass->class_object = obj;
    class_objects_[obj] = klass;
    return obj;
}

DexClass* DexClassLinker::ClassFromObject(DexObject* obj) const {
    if (obj == nullptr) return nullptr;
    auto it = class_objects_.find(obj);
    return it != class_objects_.end() ? it->second : nullptr;
}

bool DexClassLinker::IsRegisteredClass(const DexClass* klass) const {
    if (klass == nullptr) return false;
    return live_classes_.count(klass) != 0;
}

DexClass* DexClassLinker::ClassOfObject(const DexObject* obj) const {
    if (obj == nullptr) return nullptr;

    // A DexClass handed in where a DexObject was expected: reading obj->clazz would
    // read DexClass::descriptor, which shares offset 0. Check before dereferencing.
    if (live_classes_.count(reinterpret_cast<const DexClass*>(obj)) != 0) return nullptr;

    DexClass* klass = obj->clazz;
    // Validate what came out: a stale handle can carry any bit pattern, and a
    // non-null one passes the usual null check only to fault on first use.
    return IsRegisteredClass(klass) ? klass : nullptr;
}

DexClassLinker::BadReceiver DexClassLinker::ClassifyObject(const DexObject* obj) const {
    if (obj == nullptr) return BadReceiver::kNull;
    if (live_classes_.count(reinterpret_cast<const DexClass*>(obj)) != 0) {
        return BadReceiver::kIsAClass;
    }
    DexClass* klass = obj->clazz;
    if (klass == nullptr) return BadReceiver::kNullClass;
    if (!IsRegisteredClass(klass)) return BadReceiver::kUnknownClass;
    return BadReceiver::kOk;
}

std::string DexClassLinker::DescribeBadReceiver(const DexObject* obj) const {
    return DescribeBadObject(obj, "receiver");
}

std::string DexClassLinker::DescribeBadObject(const DexObject* obj, const char* role) const {
    char buf[192];
    const char* what = (role != nullptr && *role != '\0') ? role : "object";
    switch (ClassifyObject(obj)) {
        case BadReceiver::kOk:
            return std::string("the ") + what + " is valid";
        case BadReceiver::kNull:
            return std::string(what) + " is null";
        case BadReceiver::kIsAClass: {
            // Name the class, since we know exactly which one was passed. This is the
            // "raftpe/" shape: reading clazz off it would have yielded the descriptor.
            const auto* as_class = reinterpret_cast<const DexClass*>(obj);
            std::snprintf(buf, sizeof(buf),
                          "%s is the CLASS %s, not an instance of it"
                          " (a jclass was passed where a jobject was expected)",
                          what,
                          as_class->descriptor != nullptr ? as_class->descriptor : "?");
            return buf;
        }
        case BadReceiver::kNullClass:
            std::snprintf(buf, sizeof(buf),
                          "%s %p has clazz=null (allocated but never initialised)",
                          what, static_cast<const void*>(obj));
            return buf;
        case BadReceiver::kUnknownClass:
            std::snprintf(buf, sizeof(buf),
                          "%s %p has clazz=%p, which is not a class this runtime"
                          " created (stale or fabricated JNI handle)",
                          what, static_cast<const void*>(obj),
                          static_cast<const void*>(obj->clazz));
            return buf;
    }
    return std::string(what) + " is unusable";
}

}  // namespace kuart
}  // namespace kudroid
