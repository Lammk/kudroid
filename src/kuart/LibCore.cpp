#include "kudroid/kuart/LibCore.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <filesystem>
#include <functional>
#include <mutex>
#include <random>
#include <set>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include "kudroid/kudroid_bridge.h"
#include "kudroid/VFSPathRemapper.h"
#include "kudroid/kuart/DexClass.h"
#include "kudroid/kuart/DexClassLinker.h"
#include "kudroid/kuart/DexHeap.h"
#include "kudroid/kuart/DexJniEnv.h"
#include "kudroid/kuart/DexObject.h"
#include "kudroid/kuart/DexString.h"
#include "kudroid/kuart/Interpreter.h"
#include "kudroid/kuart/VmLock.h"
#include "kudroid/platform/MemoryInfo.h"
#include "kudroid/platform/CpuInfo.h"
#include "kudroid/platform/AudioShim.h"
#include "kudroid/platform/FramePacer.h"
#include "kudroid/platform/JavaCanvasRenderer.h"

namespace kudroid {
namespace kuart {

namespace {

const char* GetStringUtf8(const DexValue& val) {
    if (val.l == nullptr) return "";
    auto* str = reinterpret_cast<DexString*>(val.l);
    return str->utf8 ? str->utf8 : "";
}

DexString* AsString(const DexValue& val) {
    return reinterpret_cast<DexString*>(val.l);
}

// Number of UTF-16 code units in a UTF-8 buffer. Astral characters (4-byte
// sequences) count as 2 because Java stores them as a surrogate pair.
uint32_t Utf16Length(const char* utf8, uint32_t bytes) {
    uint32_t units = 0;
    for (uint32_t i = 0; i < bytes;) {
        const auto b = static_cast<unsigned char>(utf8[i]);
        if (b < 0x80) {
            i += 1;
            units += 1;
        } else if ((b & 0xE0) == 0xC0) {
            i += 2;
            units += 1;
        } else if ((b & 0xF0) == 0xE0) {
            i += 3;
            units += 1;
        } else {
            i += 4;
            units += 2;
        }
    }
    return units;
}

// Decodes UTF-8 into UTF-16 code units. Kept explicit rather than using the
// std::codecvt family, which is deprecated and unavailable on some toolchains.
void Utf8ToUtf16(const char* utf8, uint32_t bytes, std::vector<uint16_t>* out) {
    for (uint32_t i = 0; i < bytes;) {
        const auto b0 = static_cast<unsigned char>(utf8[i]);
        uint32_t cp;
        uint32_t len;
        if (b0 < 0x80) {
            cp = b0;
            len = 1;
        } else if ((b0 & 0xE0) == 0xC0) {
            cp = b0 & 0x1Fu;
            len = 2;
        } else if ((b0 & 0xF0) == 0xE0) {
            cp = b0 & 0x0Fu;
            len = 3;
        } else {
            cp = b0 & 0x07u;
            len = 4;
        }
        if (i + len > bytes) {
            // Truncated sequence: emit replacement char rather than reading past
            // the end of the buffer.
            out->push_back(0xFFFD);
            break;
        }
        for (uint32_t k = 1; k < len; ++k) {
            cp = (cp << 6) | (static_cast<unsigned char>(utf8[i + k]) & 0x3Fu);
        }
        i += len;
        if (cp >= 0x10000) {
            cp -= 0x10000;
            out->push_back(static_cast<uint16_t>(0xD800 + (cp >> 10)));
            out->push_back(static_cast<uint16_t>(0xDC00 + (cp & 0x3FF)));
        } else {
            out->push_back(static_cast<uint16_t>(cp));
        }
    }
}

void AppendUtf8(std::string* out, uint32_t cp) {
    if (cp < 0x80) {
        out->push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out->push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out->push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out->push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out->push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

std::string Utf16ToUtf8(const uint16_t* units, size_t count) {
    std::string out;
    out.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        uint32_t cp = units[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < count && units[i + 1] >= 0xDC00 &&
            units[i + 1] <= 0xDFFF) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (units[i + 1] - 0xDC00);
            ++i;
        }
        AppendUtf8(&out, cp);
    }
    return out;
}

// The UTF-16 view of a string. Fast path for the ASCII case (the vast majority:
// class names, log tags, file paths) avoids allocating.
class Utf16View {
public:
    explicit Utf16View(DexString* str) {
        if (str == nullptr || str->utf8 == nullptr) return;
        ascii_ = str->ascii;
        if (ascii_) {
            bytes_ = str->utf8;
            count_ = str->length;
        } else {
            Utf8ToUtf16(str->utf8, str->length, &units_);
            count_ = static_cast<uint32_t>(units_.size());
        }
    }

    uint32_t length() const { return count_; }

    uint16_t at(uint32_t index) const {
        if (index >= count_) return 0;
        return ascii_ ? static_cast<uint16_t>(static_cast<unsigned char>(bytes_[index]))
                      : units_[index];
    }

    std::string SubstringUtf8(uint32_t begin, uint32_t end) const {
        if (ascii_) return std::string(bytes_ + begin, end - begin);
        return Utf16ToUtf8(units_.data() + begin, end - begin);
    }

private:
    bool ascii_ = true;
    const char* bytes_ = nullptr;
    std::vector<uint16_t> units_;
    uint32_t count_ = 0;
};

// Overwrites the DexString payload of an already allocated object. String
// constructors are Java code calling an init* native, so the object exists
// before its characters are known.
void InitStringFrom(DexClassLinker* linker, DexObject* self, const std::string& utf8) {
    auto* str = reinterpret_cast<DexString*>(self);
    DexString* fresh = linker->NewString(utf8.c_str());
    if (fresh == nullptr) return;
    str->utf8 = fresh->utf8;
    str->length = fresh->length;
    str->ascii = fresh->ascii;
}

// ─────────────────────────────────────────────────────────────────────────────
// java.lang.Object
// ─────────────────────────────────────────────────────────────────────────────
bool Invoke_java_lang_Object(Interpreter* interp, const char* name, const DexValue* args,
                             size_t num_args, DexValue* result) {
    DexObject* self = args[0].l;
    if (self == nullptr) {
        interp->ThrowException("Ljava/lang/NullPointerException;", "null receiver");
        return true;
    }

    if (std::strcmp(name, "getClass") == 0) {
        DexClass* clazz = self->clazz ? self->clazz : interp->linker()->FindClass("Ljava/lang/Object;");
        result->l = interp->linker()->GetClassObject(clazz);
        return true;
    }
    if (std::strcmp(name, "hashCode") == 0) {
        *result = DexValue::Int(static_cast<int32_t>(reinterpret_cast<uintptr_t>(self)));
        return true;
    }
    if (std::strcmp(name, "clone") == 0) {
        DexClass* klass = self->clazz;
        if (klass == nullptr) {
            interp->ThrowException("Ljava/lang/CloneNotSupportedException;", "no class");
            return true;
        }
        if (klass->is_array) {
            auto* src = reinterpret_cast<DexArray*>(self);
            DexArray* copy = interp->linker()->AllocArray(klass, src->length);
            if (copy == nullptr) {
                interp->ThrowException("Ljava/lang/OutOfMemoryError;", "clone array");
                return true;
            }
            const uint32_t elem = DexClassLinker::ElementSize(klass->component_type);
            std::memcpy(reinterpret_cast<uint8_t*>(copy + 1),
                        reinterpret_cast<uint8_t*>(src + 1),
                        static_cast<size_t>(src->length) * elem);
            result->l = copy;
            return true;
        }
        DexObject* copy = interp->linker()->AllocObject(klass);
        if (copy == nullptr) {
            interp->ThrowException("Ljava/lang/OutOfMemoryError;", "clone object");
            return true;
        }
        std::memcpy(copy->FieldData(), self->FieldData(), klass->object_size);
        result->l = copy;
        return true;
    }
    // Monitors are real (see VmLock.h): wait/notify must be paired with an owned
    // monitor, and libcore relies on them for Thread.join and blocking queues.
    if (std::strcmp(name, "notify") == 0 || std::strcmp(name, "notifyAll") == 0) {
        if (!Monitor::Notify(self, name[6] == 'A')) {
            interp->ThrowException("Ljava/lang/IllegalMonitorStateException;",
                                   "notify without owning the monitor");
        }
        return true;
    }
    if (std::strcmp(name, "wait") == 0) {
        const int64_t millis = num_args > 1 ? args[1].j : 0;
        if (!Monitor::Wait(self, millis, 0)) {
            interp->ThrowException("Ljava/lang/IllegalMonitorStateException;",
                                   "wait without owning the monitor");
        }
        return true;
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// reflection object plumbing
// ─────────────────────────────────────────────────────────────────────────────

void SetRefField(DexObject* obj, const char* name, const char* type, DexObject* value) {
    if (obj == nullptr || obj->clazz == nullptr) return;
    if (DexField* f = obj->clazz->FindInstanceField(name, type)) {
        obj->SetField<DexObject*>(f->offset_or_slot, value);
    }
}

DexObject* GetRefField(DexObject* obj, const char* name, const char* type) {
    if (obj == nullptr || obj->clazz == nullptr) return nullptr;
    DexField* f = obj->clazz->FindInstanceField(name, type);
    return f != nullptr ? obj->GetField<DexObject*>(f->offset_or_slot) : nullptr;
}

void SetLongField(DexObject* obj, const char* name, int64_t value) {
    if (obj == nullptr || obj->clazz == nullptr) return;
    if (DexField* f = obj->clazz->FindInstanceField(name, "J")) {
        obj->SetField<int64_t>(f->offset_or_slot, value);
    }
}

int64_t GetLongField(DexObject* obj, const char* name) {
    if (obj == nullptr || obj->clazz == nullptr) return 0;
    DexField* f = obj->clazz->FindInstanceField(name, "J");
    return f != nullptr ? obj->GetField<int64_t>(f->offset_or_slot) : 0;
}

// Splits a DEX method signature into parameter descriptors: "(ILjava/lang/String;[I)V"
// becomes {"I", "Ljava/lang/String;", "[I"}.
std::vector<std::string> SplitParams(const char* signature) {
    std::vector<std::string> out;
    if (signature == nullptr || *signature != '(') return out;
    const char* p = signature + 1;
    while (*p != '\0' && *p != ')') {
        std::string one;
        while (*p == '[') {
            one += *p++;
        }
        if (*p == 'L') {
            while (*p != '\0' && *p != ';') one += *p++;
            if (*p == ';') one += *p++;
        } else if (*p != '\0') {
            one += *p++;
        }
        out.push_back(one);
    }
    return out;
}

DexArray* NewRefArray(DexClassLinker* linker, const char* array_descriptor,
                      const std::vector<DexObject*>& items) {
    DexClass* klass = linker->FindClass(array_descriptor);
    if (klass == nullptr) return nullptr;
    DexArray* arr = linker->AllocArray(klass, static_cast<int32_t>(items.size()));
    if (arr == nullptr) return nullptr;
    auto** data = reinterpret_cast<DexObject**>(arr + 1);
    for (size_t i = 0; i < items.size(); ++i) data[i] = items[i];
    return arr;
}

DexArray* ClassArrayFromDescriptors(DexClassLinker* linker,
                                    const std::vector<std::string>& descriptors) {
    std::vector<DexObject*> objects;
    objects.reserve(descriptors.size());
    for (const std::string& d : descriptors) {
        DexClass* k = linker->FindClass(d.c_str());
        objects.push_back(k != nullptr ? linker->GetClassObject(k) : nullptr);
    }
    return NewRefArray(linker, "[Ljava/lang/Class;", objects);
}

// java.lang.reflect.{Method,Field,Constructor} have private constructors: only
// native code creates them, storing the KuART pointer in the `art*` long field.
DexObject* NewReflectObject(DexClassLinker* linker, const char* descriptor,
                            const char* handle_field, const void* handle,
                            DexClass* declaring, const char* member_name) {
    DexClass* klass = linker->FindClass(descriptor);
    if (klass == nullptr) return nullptr;
    DexObject* obj = linker->AllocObject(klass);
    if (obj == nullptr) return nullptr;
    SetLongField(obj, handle_field, static_cast<int64_t>(reinterpret_cast<uintptr_t>(handle)));
    if (declaring != nullptr) {
        SetRefField(obj, "declaringClass", "Ljava/lang/Class;",
                    linker->GetClassObject(declaring));
    }
    if (member_name != nullptr) {
        SetRefField(obj, "name", "Ljava/lang/String;", linker->NewString(member_name));
    }
    return obj;
}

DexObject* NewMethodObject(DexClassLinker* linker, DexMethod* m) {
    return NewReflectObject(linker, "Ljava/lang/reflect/Method;", "artMethod", m,
                            m->declaring_class, m->name);
}

DexObject* NewConstructorObject(DexClassLinker* linker, DexMethod* m) {
    return NewReflectObject(linker, "Ljava/lang/reflect/Constructor;", "artMethod", m,
                            m->declaring_class, nullptr);
}

DexObject* NewFieldObject(DexClassLinker* linker, DexField* f) {
    return NewReflectObject(linker, "Ljava/lang/reflect/Field;", "artField", f,
                            f->declaring_class, f->name);
}

DexMethod* MethodFromObject(DexObject* obj) {
    return reinterpret_cast<DexMethod*>(
        static_cast<uintptr_t>(GetLongField(obj, "artMethod")));
}

DexField* FieldFromObject(DexObject* obj) {
    return reinterpret_cast<DexField*>(
        static_cast<uintptr_t>(GetLongField(obj, "artField")));
}

// The DexClass behind a value that is supposed to name a class.
//
// Three different things arrive here and only one of them is a plain object:
//
//   DexClassObject*  — a java.lang.Class INSTANCE, from const-class, Class.forName or
//                      Object.getClass. This is what bytecode passes.
//   DexClass*        — a raw jclass. Native code holds classes as DexClass* (see
//                      DexJniEnv.h) and a jclass IS a jobject in the JNI object model,
//                      so handing one to anything typed Class<?> is CORRECT usage.
//   anything else    — not a class at all.
//
// The middle case is why this function needs to exist rather than just reading ->clazz.
// DexClass::descriptor and DexObject::clazz both sit at offset 0, so ->clazz on a
// DexClass yields the descriptor STRING pointer, and that pointer then gets used as a
// class. This function used to do exactly that, and Unity crashed on it:
// Proxy.newProxyInstance received Class[] whose elements were raw jclass handles, each
// became a `const char*`, and GetOrCreateProxyClass appended iface->descriptor — reading
// offset 0 of the string "Landroid/..." — to its cache key. strlen() faulted at
// 0x64696f72646e6140, which is the 16-byte-aligned form of 0x64696f72646e614c, the ASCII
// bytes `Landroid`.
//
// The same shape was already fixed in DexJniEnv::CallJavaA and JNI GetObjectClass, both
// of which substitute the heap java.lang.Class object for a DexClass receiver. Fixing it
// there and not here left the reflection helpers as the one remaining way in.
//
// An unregistered pointer resolves to nullptr rather than being dereferenced: every
// caller already treats null as "not a class" and raises a Java exception naming the
// argument, which is a diagnosis instead of a signal.
DexClass* ClassOf(Interpreter* interp, DexObject* class_object) {
    if (class_object == nullptr) return nullptr;
    DexClassLinker* linker = interp->linker();
    if (linker == nullptr) return nullptr;

    // A java.lang.Class instance: the registered mapping is authoritative.
    if (DexClass* k = linker->ClassFromObject(class_object)) return k;

    // A raw jclass. Checked against the linker, never assumed — an unregistered pointer
    // that happened to be passed here must not be treated as class metadata.
    if (linker->IsRegisteredClass(reinterpret_cast<const DexClass*>(class_object))) {
        return reinterpret_cast<DexClass*>(class_object);
    }

    // Anything else is not a class, and saying so is the point.
    //
    // The old fallback was `class_object->clazz`, which answered "the class OF this
    // object" — so a String argument reported java.lang.String as though the caller had
    // passed String.class, and a jclass reported its own descriptor string. Both are
    // wrong answers dressed as right ones. Every caller here treats null as "not a
    // class" and raises a Java exception naming the argument.
    return nullptr;
}

// Boxes a primitive so reflection can return Object. `descriptor` is the field
// or return type; reference types pass through untouched.
DexObject* BoxValue(Interpreter* interp, const char* descriptor, const DexValue& v) {
    if (descriptor == nullptr || descriptor[0] == '\0') return nullptr;
    const char* box_class = nullptr;
    const char* box_sig = nullptr;
    switch (descriptor[0]) {
        case 'Z': box_class = "Ljava/lang/Boolean;";   box_sig = "(Z)Ljava/lang/Boolean;"; break;
        case 'B': box_class = "Ljava/lang/Byte;";      box_sig = "(B)Ljava/lang/Byte;"; break;
        case 'C': box_class = "Ljava/lang/Character;"; box_sig = "(C)Ljava/lang/Character;"; break;
        case 'S': box_class = "Ljava/lang/Short;";     box_sig = "(S)Ljava/lang/Short;"; break;
        case 'I': box_class = "Ljava/lang/Integer;";   box_sig = "(I)Ljava/lang/Integer;"; break;
        case 'J': box_class = "Ljava/lang/Long;";      box_sig = "(J)Ljava/lang/Long;"; break;
        case 'F': box_class = "Ljava/lang/Float;";     box_sig = "(F)Ljava/lang/Float;"; break;
        case 'D': box_class = "Ljava/lang/Double;";    box_sig = "(D)Ljava/lang/Double;"; break;
        default: return v.l;  // already a reference
    }
    DexClass* klass = interp->linker()->FindClass(box_class);
    if (klass == nullptr) return nullptr;
    interp->EnsureInitialized(klass);
    DexMethod* value_of = klass->FindDirectMethod("valueOf", box_sig);
    if (value_of == nullptr) return nullptr;
    DexValue arg = v;
    return interp->Execute(value_of, &arg, 1).l;
}

// Inverse of BoxValue: unwraps a boxed primitive for a field/parameter of type
// `descriptor`. Returns false when the object is not the expected box type.
bool UnboxValue(Interpreter* interp, const char* descriptor, DexObject* obj, DexValue* out) {
    if (descriptor == nullptr || descriptor[0] == '\0') return false;
    const char* getter = nullptr;
    const char* getter_sig = nullptr;
    switch (descriptor[0]) {
        case 'Z': getter = "booleanValue"; getter_sig = "()Z"; break;
        case 'B': getter = "byteValue";    getter_sig = "()B"; break;
        case 'C': getter = "charValue";    getter_sig = "()C"; break;
        case 'S': getter = "shortValue";   getter_sig = "()S"; break;
        case 'I': getter = "intValue";     getter_sig = "()I"; break;
        case 'J': getter = "longValue";    getter_sig = "()J"; break;
        case 'F': getter = "floatValue";   getter_sig = "()F"; break;
        case 'D': getter = "doubleValue";  getter_sig = "()D"; break;
        default:
            *out = DexValue::Ref(obj);
            return true;
    }
    if (obj == nullptr || obj->clazz == nullptr) return false;
    DexMethod* m = obj->clazz->FindVirtualMethod(getter, getter_sig);
    if (m == nullptr) return false;
    DexValue self = DexValue::Ref(obj);
    *out = interp->Execute(m, &self, 1);
    return true;
}

// The process-wide ClassLoader instance.
//
// KuART resolves classes through a single DexClassLinker, so there is one loader.
// It still has to be a real object: app code calls
// SomeClass.class.getClassLoader().loadClass(name), and a null loader turns that
// into a NullPointerException inside a <clinit>.
//
// Built by calling ClassLoader.getSystemClassLoader() so Java code that asks
// directly and native code that asks through Class.getClassLoader() get the same
// instance, exactly as on Android.
DexObject* SystemClassLoaderObject(Interpreter* interp) {
    static DexObject* loader = nullptr;
    if (loader != nullptr) return loader;

    DexClass* klass = interp->linker()->FindClass("Ljava/lang/ClassLoader;");
    if (klass == nullptr || klass->is_stub) return nullptr;
    if (!interp->EnsureInitialized(klass)) return nullptr;

    if (DexMethod* get = klass->FindDirectMethod("getSystemClassLoader",
                                                 "()Ljava/lang/ClassLoader;")) {
        const DexValue r = interp->Execute(get, nullptr, 0);
        if (!interp->HasPendingException() && r.l != nullptr) {
            loader = r.l;
            return loader;
        }
        // A failure here would otherwise be attributed to the caller's own work.
        interp->ClearPendingException();
    }

    // Fall back to a bare instance: a loader that resolves through the linker is
    // still better than null, which crashes the caller outright.
    loader = interp->linker()->AllocObject(klass);
    return loader;
}

// ─────────────────────────────────────────────────────────────────────────────
// java.lang.Class
// ─────────────────────────────────────────────────────────────────────────────
bool Invoke_java_lang_Class(Interpreter* interp, const char* name, const DexValue* args,
                            size_t num_args, DexValue* result) {
    // Static, so it must be handled before args[0] is read as a receiver.
    //
    // Backs every primitive class literal in guest bytecode: javac turns int.class into a
    // read of Integer.TYPE, and each box class initialises TYPE from here. The names are
    // Java's source spellings rather than DEX descriptors because that is what the box
    // classes pass and what the platform's own Class.getPrimitiveClass takes.
    if (std::strcmp(name, "getPrimitiveClass") == 0) {
        const char* prim = num_args > 0 ? GetStringUtf8(args[0]) : nullptr;
        if (prim == nullptr || prim[0] == '\0') {
            interp->ThrowException("Ljava/lang/IllegalArgumentException;",
                                   "getPrimitiveClass with no name");
            return true;
        }
        // Source name -> DEX descriptor. The linker already manufactures a DexClass for a
        // one-character primitive descriptor (DexClassLinker::CreatePrimitiveClass), so
        // there is nothing to build here beyond the translation.
        const char* descriptor = nullptr;
        if (std::strcmp(prim, "int") == 0) descriptor = "I";
        else if (std::strcmp(prim, "long") == 0) descriptor = "J";
        else if (std::strcmp(prim, "short") == 0) descriptor = "S";
        else if (std::strcmp(prim, "byte") == 0) descriptor = "B";
        else if (std::strcmp(prim, "char") == 0) descriptor = "C";
        else if (std::strcmp(prim, "float") == 0) descriptor = "F";
        else if (std::strcmp(prim, "double") == 0) descriptor = "D";
        else if (std::strcmp(prim, "boolean") == 0) descriptor = "Z";
        else if (std::strcmp(prim, "void") == 0) descriptor = "V";
        if (descriptor == nullptr) {
            // Not a primitive name. Naming the argument matters: the only way to get here
            // is a caller that invented a name, and "int " or "Integer" both look right.
            interp->ThrowException("Ljava/lang/IllegalArgumentException;",
                                   std::string("not a primitive type: ") + prim);
            return true;
        }
        DexClass* klass = interp->linker()->FindClass(descriptor);
        if (klass == nullptr) {
            interp->ThrowException("Ljava/lang/InternalError;",
                                   std::string("cannot create primitive class ") + prim);
            return true;
        }
        result->l = interp->linker()->GetClassObject(klass);
        return true;
    }

    if (std::strcmp(name, "forName") == 0) {
        const char* class_name = GetStringUtf8(args[0]);
        if (class_name == nullptr || class_name[0] == '\0') {
            interp->ThrowException("Ljava/lang/ClassNotFoundException;", "<empty>");
            return true;
        }
        std::string desc;
        if (class_name[0] == '[') {
            desc = class_name;
        } else {
            desc = "L";
            for (const char* p = class_name; *p; ++p) {
                desc += (*p == '.') ? '/' : *p;
            }
            desc += ";";
        }
        DexClass* klass = interp->linker()->FindClass(desc.c_str());
        // A stub is a placeholder for a boot-classpath class KuDroid does not ship
        // yet; it has no methods or fields, so reporting success here would hand
        // the caller an unusable Class and surface the real problem much later as
        // a ClassCastException or a zero-valued field.
        if (klass == nullptr || klass->is_stub) {
            interp->ThrowException("Ljava/lang/ClassNotFoundException;", class_name);
            return true;
        }
        interp->EnsureInitialized(klass);
        result->l = interp->linker()->GetClassObject(klass);
        return true;
    }

    DexObject* self = args[0].l;
    if (self == nullptr) {
        interp->ThrowException("Ljava/lang/NullPointerException;", "null class object");
        return true;
    }
    DexClassLinker* linker = interp->linker();
    DexClass* klass = ClassOf(interp, self);

    if (std::strcmp(name, "getName") == 0) {
        std::string pretty = klass ? klass->PrettyName() : "java.lang.Object";
        result->l = linker->NewString(pretty.c_str());
        return true;
    }
    if (std::strcmp(name, "getSuperclass") == 0) {
        if (klass && klass->superclass) {
            result->l = linker->GetClassObject(klass->superclass);
        } else {
            result->l = nullptr;
        }
        return true;
    }
    if (std::strcmp(name, "isInterface") == 0) {
        *result = DexValue::Int(klass && klass->IsInterface() ? 1 : 0);
        return true;
    }
    if (std::strcmp(name, "isArray") == 0) {
        *result = DexValue::Int(klass && klass->is_array ? 1 : 0);
        return true;
    }
    if (std::strcmp(name, "isPrimitive") == 0) {
        *result = DexValue::Int(klass && klass->is_primitive ? 1 : 0);
        return true;
    }
    if (std::strcmp(name, "isEnum") == 0) {
        // No ACC_ENUM bit is exposed here; enum classes are exactly those whose
        // superclass chain reaches java.lang.Enum.
        DexClass* super = klass != nullptr ? klass->superclass : nullptr;
        bool is_enum = false;
        while (super != nullptr) {
            if (super->descriptor != nullptr &&
                std::strcmp(super->descriptor, "Ljava/lang/Enum;") == 0) {
                is_enum = true;
                break;
            }
            super = super->superclass;
        }
        *result = DexValue::Int(is_enum ? 1 : 0);
        return true;
    }
    if (std::strcmp(name, "getModifiers") == 0) {
        *result = DexValue::Int(klass != nullptr ? static_cast<int32_t>(klass->access_flags) : 0);
        return true;
    }
    if (std::strcmp(name, "getClassLoader") == 0) {
        // KuART resolves every class through one DexClassLinker, so there is only
        // ever one loader — but it must be a real object, not null.
        //
        // Returning null used to look harmless because KuDroid's own code never
        // asks. App code does: the common obfuscator pattern is
        // SomeClass.class.getClassLoader().loadClass("...") to reach a class by
        // name, and on null that is an immediate NullPointerException inside a
        // <clinit>, which poisons the class for the rest of the process. Any app
        // built with that tooling cannot start.
        //
        // The instance comes from ClassLoader.getSystemClassLoader() so there is
        // exactly one, shared with what Java code obtains directly.
        result->l = SystemClassLoaderObject(interp);
        return true;
    }
    if (std::strcmp(name, "getComponentType") == 0) {
        if (klass && klass->component_type) {
            result->l = linker->GetClassObject(klass->component_type);
        } else {
            result->l = nullptr;
        }
        return true;
    }
    if (std::strcmp(name, "getInterfaces") == 0) {
        std::vector<DexObject*> objects;
        if (klass != nullptr) {
            for (DexClass* iface : klass->interfaces) {
                objects.push_back(iface != nullptr ? linker->GetClassObject(iface) : nullptr);
            }
        }
        result->l = NewRefArray(linker, "[Ljava/lang/Class;", objects);
        return true;
    }
    if (std::strcmp(name, "getDeclaredMethods") == 0) {
        std::vector<DexObject*> objects;
        if (klass != nullptr) {
            for (DexMethod& m : klass->direct_methods) {
                if (m.IsConstructor()) continue;
                objects.push_back(NewMethodObject(linker, &m));
            }
            for (DexMethod& m : klass->virtual_methods) {
                objects.push_back(NewMethodObject(linker, &m));
            }
            // A proxy class declares the methods of every interface it was created
            // for. KuART's proxy carries no DexMethods of its own — dispatch reaches
            // the InvocationHandler through DexClass::is_proxy instead — so without
            // this a proxy for Runnable reported an EMPTY method list. Unity's
            // JNIBridge enumerates that list to decide whether a method exists, ran
            // its iterator dry (the NoSuchElementException burst in the log) and
            // then threw NoSuchMethodError("java.lang.Runnable.run()") through JNI
            // ThrowNew, cleared it, and rethrew an empty one from
            // bitter.jnibridge.a.invoke — which killed ActivityThread with nothing
            // in the log naming the cause.
            //
            // The Method objects report the INTERFACE as their declaring class,
            // which is also what the handler receives from InvokeProxyMethod. Real
            // $ProxyN names itself, but one answer in two places would be worse than
            // one that is merely less specific.
            if (klass->is_proxy) {
                std::vector<DexClass*> pending(klass->interfaces.begin(),
                                               klass->interfaces.end());
                std::set<DexClass*> visited;
                while (!pending.empty()) {
                    DexClass* iface = pending.back();
                    pending.pop_back();
                    if (iface == nullptr || !visited.insert(iface).second) continue;
                    for (DexMethod& m : iface->virtual_methods) {
                        objects.push_back(NewMethodObject(linker, &m));
                    }
                    for (DexClass* super_iface : iface->interfaces) {
                        pending.push_back(super_iface);
                    }
                }
            }
        }
        result->l = NewRefArray(linker, "[Ljava/lang/reflect/Method;", objects);
        return true;
    }
    if (std::strcmp(name, "getDeclaredFields") == 0) {
        std::vector<DexObject*> objects;
        if (klass != nullptr) {
            for (DexField& f : klass->static_fields) objects.push_back(NewFieldObject(linker, &f));
            for (DexField& f : klass->instance_fields) objects.push_back(NewFieldObject(linker, &f));
        }
        result->l = NewRefArray(linker, "[Ljava/lang/reflect/Field;", objects);
        return true;
    }
    if (std::strcmp(name, "getDeclaredConstructors") == 0) {
        std::vector<DexObject*> objects;
        if (klass != nullptr) {
            for (DexMethod& m : klass->direct_methods) {
                if (m.name != nullptr && std::strcmp(m.name, "<init>") == 0) {
                    objects.push_back(NewConstructorObject(linker, &m));
                }
            }
        }
        result->l = NewRefArray(linker, "[Ljava/lang/reflect/Constructor;", objects);
        return true;
    }
    if (std::strcmp(name, "isAssignableFrom") == 0) {
        DexClass* other_klass = ClassOf(interp, num_args > 1 ? args[1].l : nullptr);
        *result = DexValue::Int((klass && other_klass && other_klass->IsSubClassOf(klass)) ? 1 : 0);
        return true;
    }
    if (std::strcmp(name, "isInstance") == 0) {
        DexObject* target = num_args > 1 ? args[1].l : nullptr;
        *result = DexValue::Int((klass && target && target->clazz && target->clazz->IsSubClassOf(klass)) ? 1 : 0);
        return true;
    }
    if (std::strcmp(name, "newInstance") == 0) {
        if (klass == nullptr || klass->IsInterface() || klass->IsAbstract()) {
            interp->ThrowException("Ljava/lang/InstantiationException;", klass ? klass->PrettyName() : "null");
            return true;
        }
        // Stubs have no members; see DexClass::is_stub.
        if (klass->is_stub) {
            interp->ThrowException("Ljava/lang/NoClassDefFoundError;", klass->PrettyName());
            return true;
        }
        interp->EnsureInitialized(klass);
        DexObject* new_obj = linker->AllocObject(klass);
        if (new_obj == nullptr) {
            interp->ThrowException("Ljava/lang/OutOfMemoryError;", "AllocObject failed");
            return true;
        }
        DexMethod* ctor = klass->FindDirectMethod("<init>", "()V");
        if (ctor != nullptr) {
            DexValue ctor_arg = DexValue::Ref(new_obj);
            interp->Execute(ctor, &ctor_arg, 1);
        }
        result->l = new_obj;
        return true;
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// java.lang.reflect.{Method,Constructor,Field,Array}
// ─────────────────────────────────────────────────────────────────────────────

const char* ReturnDescriptor(const DexMethod* m) {
    if (m == nullptr || m->signature == nullptr) return "V";
    const char* close = std::strchr(m->signature, ')');
    return close != nullptr ? close + 1 : "V";
}

// Turns the Object[] of a reflective call into the DexValue array the
// interpreter expects, unboxing each primitive parameter.
bool BuildInvokeArgs(Interpreter* interp, DexMethod* target, DexObject* receiver,
                     DexArray* boxed, std::vector<DexValue>* out) {
    const std::vector<std::string> params = SplitParams(target->signature);
    const int32_t given = boxed != nullptr ? boxed->length : 0;
    if (static_cast<size_t>(given) != params.size()) {
        interp->ThrowException("Ljava/lang/IllegalArgumentException;",
                               "wrong argument count: expected " +
                                   std::to_string(params.size()) + ", got " +
                                   std::to_string(given));
        return false;
    }
    if (!target->IsStatic()) out->push_back(DexValue::Ref(receiver));

    auto** items = boxed != nullptr ? reinterpret_cast<DexObject**>(boxed + 1) : nullptr;
    for (size_t i = 0; i < params.size(); ++i) {
        DexValue v;
        if (!UnboxValue(interp, params[i].c_str(), items[i], &v)) {
            interp->ThrowException("Ljava/lang/IllegalArgumentException;",
                                   "argument " + std::to_string(i) + " is not a " + params[i]);
            return false;
        }
        out->push_back(v);
    }
    return true;
}

bool Invoke_java_lang_reflect_Method(Interpreter* interp, const char* name,
                                     const DexValue* args, size_t num_args,
                                     DexValue* result) {
    DexClassLinker* linker = interp->linker();
    DexMethod* m = MethodFromObject(args[0].l);
    if (m == nullptr) {
        interp->ThrowException("Ljava/lang/NullPointerException;", "method handle null");
        return true;
    }

    if (std::strcmp(name, "getModifiers") == 0) {
        *result = DexValue::Int(static_cast<int32_t>(m->access_flags));
        return true;
    }
    // An interface method with a body. The three conditions are the platform's, and each
    // one is load-bearing: an abstract interface method has nothing to call, and a static
    // interface method is not inherited so it is not a default either.
    if (std::strcmp(name, "isDefault") == 0) {
        const bool in_interface =
            m->declaring_class != nullptr && m->declaring_class->IsInterface();
        const bool is_default = in_interface && !m->IsStatic() && !m->IsAbstract();
        *result = DexValue::Int(is_default ? 1 : 0);
        return true;
    }
    if (std::strcmp(name, "isBridge") == 0) {
        *result = DexValue::Int((m->access_flags & art::kAccBridge) != 0 ? 1 : 0);
        return true;
    }
    if (std::strcmp(name, "isSynthetic") == 0) {
        *result = DexValue::Int((m->access_flags & art::kAccSynthetic) != 0 ? 1 : 0);
        return true;
    }
    if (std::strcmp(name, "isVarArgs") == 0) {
        *result = DexValue::Int((m->access_flags & art::kAccVarargs) != 0 ? 1 : 0);
        return true;
    }
    if (std::strcmp(name, "getReturnType") == 0) {
        DexClass* k = linker->FindClass(ReturnDescriptor(m));
        result->l = k != nullptr ? linker->GetClassObject(k) : nullptr;
        return true;
    }
    if (std::strcmp(name, "getParameterTypes") == 0) {
        result->l = ClassArrayFromDescriptors(linker, SplitParams(m->signature));
        return true;
    }
    if (std::strcmp(name, "invoke") == 0) {
        DexObject* receiver = num_args > 1 ? args[1].l : nullptr;
        auto* boxed = num_args > 2 ? reinterpret_cast<DexArray*>(args[2].l) : nullptr;

        // Diagnostic: Unity's JNIBridge reflects every Java call from C# through
        // here, and when it throws a messageless NoSuchMethodError the only way
        // to know which method died is this line. The last REFLECT line before
        // the uncaught trace is the prime suspect. Grep REFLECT in stderr.log.
        {
            const char* owner = (m->declaring_class != nullptr &&
                                 m->declaring_class->descriptor != nullptr)
                                    ? m->declaring_class->descriptor
                                    : "?";
            std::fprintf(stderr, "[KuART][REFLECT] invoke %s.%s%s\n", owner,
                         m->name != nullptr ? m->name : "?",
                         m->signature != nullptr ? m->signature : "?");
        }

        if (!m->IsStatic() && receiver == nullptr) {
            interp->ThrowException("Ljava/lang/NullPointerException;",
                                   std::string("invoke ") + m->name + " on null");
            return true;
        }
        // Virtual dispatch: the Method handle may come from a superclass while
        // the receiver overrides it. Validate the receiver's class instead of only
        // null-checking it — a Method.invoke reachable from JNI can be handed a
        // native-supplied receiver (see DexClassLinker::ClassOfObject).
        DexMethod* target = m;
        if (!m->IsStatic() && m->vtable_index != DexMethod::kInvalidVTableIndex) {
            if (DexClass* receiver_class = interp->linker()->ClassOfObject(receiver)) {
                if (DexMethod* found = receiver_class->FindVirtualMethod(m->name, m->signature)) {
                    target = found;
                }
            }
        }
        if (target->IsStatic()) interp->EnsureInitialized(target->declaring_class);

        std::vector<DexValue> call_args;
        if (!BuildInvokeArgs(interp, target, receiver, boxed, &call_args)) return true;

        const DexValue ret = interp->Execute(target, call_args.data(), call_args.size());
        if (interp->HasPendingException()) {
            const char* owner = (target->declaring_class != nullptr &&
                                 target->declaring_class->descriptor != nullptr)
                                    ? target->declaring_class->descriptor
                                    : "?";
            std::fprintf(stderr, "[KuART][REFLECT] invoke FAILED %s.%s%s: %s\n",
                         owner, target->name != nullptr ? target->name : "?",
                         target->signature != nullptr ? target->signature : "?",
                         interp->last_error().c_str());
            return true;
        }

        const char* ret_desc = ReturnDescriptor(target);
        result->l = (ret_desc[0] == 'V') ? nullptr : BoxValue(interp, ret_desc, ret);
        return true;
    }
    return false;
}

bool Invoke_java_lang_reflect_Constructor(Interpreter* interp, const char* name,
                                          const DexValue* args, size_t num_args,
                                          DexValue* result) {
    DexClassLinker* linker = interp->linker();
    DexMethod* ctor = MethodFromObject(args[0].l);
    if (ctor == nullptr) {
        interp->ThrowException("Ljava/lang/NullPointerException;", "constructor handle null");
        return true;
    }

    if (std::strcmp(name, "getModifiers") == 0) {
        *result = DexValue::Int(static_cast<int32_t>(ctor->access_flags));
        return true;
    }
    if (std::strcmp(name, "getParameterTypes") == 0) {
        result->l = ClassArrayFromDescriptors(linker, SplitParams(ctor->signature));
        return true;
    }
    if (std::strcmp(name, "newInstance") == 0) {
        DexClass* klass = ctor->declaring_class;
        if (klass != nullptr && klass->is_stub) {
            // Stubs have no members; see DexClass::is_stub.
            interp->ThrowException("Ljava/lang/NoClassDefFoundError;", klass->PrettyName());
            return true;
        }
        if (klass == nullptr || klass->IsInterface() || klass->IsAbstract()) {
            interp->ThrowException("Ljava/lang/InstantiationException;",
                                   klass != nullptr ? klass->PrettyName() : "null");
            return true;
        }
        interp->EnsureInitialized(klass);
        DexObject* obj = linker->AllocObject(klass);
        if (obj == nullptr) {
            interp->ThrowException("Ljava/lang/OutOfMemoryError;", "newInstance");
            return true;
        }
        auto* boxed = num_args > 1 ? reinterpret_cast<DexArray*>(args[1].l) : nullptr;
        std::vector<DexValue> call_args;
        call_args.push_back(DexValue::Ref(obj));
        const std::vector<std::string> params = SplitParams(ctor->signature);
        const int32_t given = boxed != nullptr ? boxed->length : 0;
        if (static_cast<size_t>(given) != params.size()) {
            interp->ThrowException("Ljava/lang/IllegalArgumentException;",
                                   "wrong argument count for <init>");
            return true;
        }
        auto** items = boxed != nullptr ? reinterpret_cast<DexObject**>(boxed + 1) : nullptr;
        for (size_t i = 0; i < params.size(); ++i) {
            DexValue v;
            if (!UnboxValue(interp, params[i].c_str(), items[i], &v)) {
                interp->ThrowException("Ljava/lang/IllegalArgumentException;",
                                       "argument " + std::to_string(i) + " is not a " + params[i]);
                return true;
            }
            call_args.push_back(v);
        }
        interp->Execute(ctor, call_args.data(), call_args.size());
        if (interp->HasPendingException()) return true;
        result->l = obj;
        return true;
    }
    return false;
}

bool Invoke_java_lang_reflect_Field(Interpreter* interp, const char* name,
                                    const DexValue* args, size_t num_args,
                                    DexValue* result) {
    DexClassLinker* linker = interp->linker();
    DexField* f = FieldFromObject(args[0].l);
    if (f == nullptr) {
        interp->ThrowException("Ljava/lang/NullPointerException;", "field handle null");
        return true;
    }

    if (std::strcmp(name, "getModifiers") == 0) {
        *result = DexValue::Int(static_cast<int32_t>(f->access_flags));
        return true;
    }
    if (std::strcmp(name, "getType") == 0) {
        DexClass* k = linker->FindClass(f->type_descriptor);
        result->l = k != nullptr ? linker->GetClassObject(k) : nullptr;
        return true;
    }

    const bool is_get = std::strcmp(name, "get") == 0;
    const bool is_set = std::strcmp(name, "set") == 0;
    if (!is_get && !is_set) return false;

    DexObject* target = num_args > 1 ? args[1].l : nullptr;
    if (f->IsStatic()) {
        DexClass* owner = f->declaring_class;
        if (owner == nullptr || f->offset_or_slot >= owner->static_values.size()) {
            interp->ThrowException("Ljava/lang/NoSuchFieldError;", f->name);
            return true;
        }
        interp->EnsureInitialized(owner);
        DexValue& slot = owner->static_values[f->offset_or_slot];
        if (is_get) {
            result->l = BoxValue(interp, f->type_descriptor, slot);
        } else {
            DexValue v;
            if (!UnboxValue(interp, f->type_descriptor, num_args > 2 ? args[2].l : nullptr, &v)) {
                interp->ThrowException("Ljava/lang/IllegalArgumentException;",
                                       std::string("value is not a ") + f->type_descriptor);
                return true;
            }
            slot = v;
        }
        return true;
    }

    if (target == nullptr) {
        interp->ThrowException("Ljava/lang/NullPointerException;",
                               std::string("instance field ") + f->name + " on null");
        return true;
    }

    // Reads and writes go through the field's descriptor: a plain 8-byte copy
    // would read past the end of a byte/short field.
    const char kind = f->type_descriptor != nullptr ? f->type_descriptor[0] : 'L';
    const uint32_t off = f->offset_or_slot;
    if (is_get) {
        DexValue v;
        switch (kind) {
            case 'Z': v = DexValue::Int(target->GetField<uint8_t>(off) != 0 ? 1 : 0); break;
            case 'B': v = DexValue::Int(target->GetField<int8_t>(off)); break;
            case 'C': v = DexValue::Int(target->GetField<uint16_t>(off)); break;
            case 'S': v = DexValue::Int(target->GetField<int16_t>(off)); break;
            case 'I': v = DexValue::Int(target->GetField<int32_t>(off)); break;
            case 'J': v = DexValue::Long(target->GetField<int64_t>(off)); break;
            case 'F': v = DexValue::Float(target->GetField<float>(off)); break;
            case 'D': v = DexValue::Double(target->GetField<double>(off)); break;
            default:  v = DexValue::Ref(target->GetField<DexObject*>(off)); break;
        }
        result->l = BoxValue(interp, f->type_descriptor, v);
        return true;
    }

    DexValue v;
    if (!UnboxValue(interp, f->type_descriptor, num_args > 2 ? args[2].l : nullptr, &v)) {
        interp->ThrowException("Ljava/lang/IllegalArgumentException;",
                               std::string("value is not a ") + f->type_descriptor);
        return true;
    }
    switch (kind) {
        case 'Z': target->SetField<uint8_t>(off, v.i != 0 ? 1 : 0); break;
        case 'B': target->SetField<int8_t>(off, static_cast<int8_t>(v.i)); break;
        case 'C': target->SetField<uint16_t>(off, static_cast<uint16_t>(v.i)); break;
        case 'S': target->SetField<int16_t>(off, static_cast<int16_t>(v.i)); break;
        case 'I': target->SetField<int32_t>(off, v.i); break;
        case 'J': target->SetField<int64_t>(off, v.j); break;
        case 'F': target->SetField<float>(off, v.f); break;
        case 'D': target->SetField<double>(off, v.d); break;
        default:  target->SetField<DexObject*>(off, v.l); break;
    }
    return true;
}

// Allocate an n-dimensional array. Recursive: the outer array holds references to
// arrays one dimension smaller, which is how the JVM represents T[a][b].
DexArray* AllocMultiArray(Interpreter* interp, const std::string& component_descriptor,
                          const int32_t* dims, size_t ndims) {
    DexClassLinker* linker = interp->linker();
    if (ndims == 0) return nullptr;

    // Descriptor for this level: one '[' per remaining dimension.
    std::string desc;
    desc.reserve(ndims + component_descriptor.size());
    for (size_t i = 0; i < ndims; ++i) desc += '[';
    desc += component_descriptor;

    DexClass* array_class = linker->FindClass(desc.c_str());
    if (array_class == nullptr) return nullptr;

    DexArray* arr = linker->AllocArray(array_class, dims[0]);
    if (arr == nullptr || ndims == 1) return arr;

    for (int32_t i = 0; i < dims[0]; ++i) {
        DexArray* sub = AllocMultiArray(interp, component_descriptor, dims + 1, ndims - 1);
        if (sub == nullptr) return nullptr;
        arr->Set<DexObject*>(i, sub);
    }
    return arr;
}

bool Invoke_java_lang_reflect_Array(Interpreter* interp, const DexMethod* method,
                                    const char* name, const DexValue* args,
                                    size_t num_args, DexValue* result) {
    DexClassLinker* linker = interp->linker();

    if (std::strcmp(name, "newInstance") == 0) {
        DexClass* component = ClassOf(interp, args[0].l);
        if (component == nullptr || component->descriptor == nullptr) {
            interp->ThrowException("Ljava/lang/IllegalArgumentException;", "null component type");
            return true;
        }

        // Two overloads share this name. DEX has no multianewarray opcode, so d8
        // compiles `new T[a][b]` into newInstance(Class, int[]) — dispatch on the
        // declared signature rather than guessing from the argument value, which
        // would confuse an int[] handle with an int.
        const bool multi = method != nullptr && method->signature != nullptr &&
                           std::strstr(method->signature, "[I)") != nullptr;

        if (multi) {
            auto* dims_arr = reinterpret_cast<DexArray*>(num_args > 1 ? args[1].l : nullptr);
            if (dims_arr == nullptr || dims_arr->length <= 0) {
                interp->ThrowException("Ljava/lang/IllegalArgumentException;",
                                       "empty dimensions array");
                return true;
            }
            if (dims_arr->length > 255) {
                interp->ThrowException("Ljava/lang/IllegalArgumentException;",
                                       "too many dimensions");
                return true;
            }
            std::vector<int32_t> dims(static_cast<size_t>(dims_arr->length));
            for (int32_t i = 0; i < dims_arr->length; ++i) {
                dims[static_cast<size_t>(i)] = dims_arr->Get<int32_t>(i);
                if (dims[static_cast<size_t>(i)] < 0) {
                    interp->ThrowException("Ljava/lang/NegativeArraySizeException;",
                                           std::to_string(dims[static_cast<size_t>(i)]));
                    return true;
                }
            }
            result->l = AllocMultiArray(interp, component->descriptor, dims.data(),
                                        dims.size());
            if (result->l == nullptr) {
                interp->ThrowException("Ljava/lang/OutOfMemoryError;",
                                       "Array.newInstance multi-dimensional");
            }
            return true;
        }

        const int32_t length = num_args > 1 ? args[1].i : 0;
        if (length < 0) {
            interp->ThrowException("Ljava/lang/NegativeArraySizeException;",
                                   std::to_string(length));
            return true;
        }
        DexClass* array_class = linker->FindClass((std::string("[") + component->descriptor).c_str());
        result->l = array_class != nullptr ? linker->AllocArray(array_class, length) : nullptr;
        return true;
    }

    auto* arr = reinterpret_cast<DexArray*>(args[0].l);
    if (arr == nullptr || arr->clazz == nullptr || !arr->clazz->is_array) {
        interp->ThrowException("Ljava/lang/IllegalArgumentException;", "not an array");
        return true;
    }

    if (std::strcmp(name, "getLength") == 0) {
        *result = DexValue::Int(arr->length);
        return true;
    }

    const bool is_get = std::strcmp(name, "get") == 0;
    const bool is_set = std::strcmp(name, "set") == 0;
    if (!is_get && !is_set) return false;

    const int32_t index = num_args > 1 ? args[1].i : 0;
    if (index < 0 || index >= arr->length) {
        interp->ThrowException("Ljava/lang/ArrayIndexOutOfBoundsException;",
                               std::to_string(index));
        return true;
    }
    DexClass* component = arr->clazz->component_type;
    const char* desc = component != nullptr ? component->descriptor : "Ljava/lang/Object;";
    const char kind = desc != nullptr ? desc[0] : 'L';
    auto* base = reinterpret_cast<uint8_t*>(arr + 1);

    if (is_get) {
        DexValue v;
        switch (kind) {
            case 'Z': case 'B': v = DexValue::Int(reinterpret_cast<int8_t*>(base)[index]); break;
            case 'C': v = DexValue::Int(reinterpret_cast<uint16_t*>(base)[index]); break;
            case 'S': v = DexValue::Int(reinterpret_cast<int16_t*>(base)[index]); break;
            case 'I': v = DexValue::Int(reinterpret_cast<int32_t*>(base)[index]); break;
            case 'J': v = DexValue::Long(reinterpret_cast<int64_t*>(base)[index]); break;
            case 'F': v = DexValue::Float(reinterpret_cast<float*>(base)[index]); break;
            case 'D': v = DexValue::Double(reinterpret_cast<double*>(base)[index]); break;
            default:  v = DexValue::Ref(reinterpret_cast<DexObject**>(base)[index]); break;
        }
        result->l = BoxValue(interp, desc, v);
        return true;
    }

    DexValue v;
    if (!UnboxValue(interp, desc, num_args > 2 ? args[2].l : nullptr, &v)) {
        interp->ThrowException("Ljava/lang/IllegalArgumentException;",
                               std::string("value is not a ") + desc);
        return true;
    }
    switch (kind) {
        case 'Z': case 'B': reinterpret_cast<int8_t*>(base)[index] = static_cast<int8_t>(v.i); break;
        case 'C': reinterpret_cast<uint16_t*>(base)[index] = static_cast<uint16_t>(v.i); break;
        case 'S': reinterpret_cast<int16_t*>(base)[index] = static_cast<int16_t>(v.i); break;
        case 'I': reinterpret_cast<int32_t*>(base)[index] = v.i; break;
        case 'J': reinterpret_cast<int64_t*>(base)[index] = v.j; break;
        case 'F': reinterpret_cast<float*>(base)[index] = v.f; break;
        case 'D': reinterpret_cast<double*>(base)[index] = v.d; break;
        default:  reinterpret_cast<DexObject**>(base)[index] = v.l; break;
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// java.lang.reflect.Proxy
// ─────────────────────────────────────────────────────────────────────────────

// newProxyInstance(ClassLoader, Class[], InvocationHandler).
//
// The Java side cannot do this itself: it would have to generate a class
// implementing the requested interfaces, which needs a class writer KuART does not
// have. Instead the linker synthesises a bodyless class carrying those interfaces
// (DexClassLinker::GetOrCreateProxyClass) and the interpreter forwards calls on it
// to the handler.
//
// The previous Java implementation returned `new Proxy(h)` — an object that does not
// implement the requested interface and has no method bodies — so the checked cast at
// the call site either failed or, when the DEX had no cast, the first interface call
// died with AbstractMethodError.
bool Invoke_java_lang_reflect_Proxy(Interpreter* interp, const char* name,
                                    const DexValue* args, size_t num_args,
                                    DexValue* result) {
    DexClassLinker* linker = interp->linker();

    const auto collect_interfaces = [&](DexObject* array_obj,
                                        std::vector<DexClass*>* out) -> bool {
        auto* arr = reinterpret_cast<DexArray*>(array_obj);
        if (arr == nullptr) return true;  // no interfaces is legal, if useless
        auto** items = reinterpret_cast<DexObject**>(arr + 1);
        for (int32_t i = 0; i < arr->length; ++i) {
            DexClass* iface = ClassOf(interp, items[i]);
            if (iface == nullptr) {
                interp->ThrowException("Ljava/lang/IllegalArgumentException;",
                                       "interface " + std::to_string(i) +
                                           " is not a class");
                return false;
            }
            out->push_back(iface);
        }
        return true;
    };

    if (std::strcmp(name, "newProxyInstance") == 0) {
        // (ClassLoader, Class[], InvocationHandler) — static, so args[0] is the loader.
        DexObject* handler = num_args > 2 ? args[2].l : nullptr;
        if (handler == nullptr) {
            interp->ThrowException("Ljava/lang/NullPointerException;",
                                   "InvocationHandler is null");
            return true;
        }
        std::vector<DexClass*> interfaces;
        if (!collect_interfaces(num_args > 1 ? args[1].l : nullptr, &interfaces)) return true;

        DexClass* proxy_class = linker->GetOrCreateProxyClass(interfaces);
        if (proxy_class == nullptr) {
            interp->ThrowException("Ljava/lang/IllegalArgumentException;",
                                   "cannot create proxy class: " + linker->last_error());
            return true;
        }
        DexObject* proxy = linker->AllocObject(proxy_class);
        if (proxy == nullptr) {
            interp->ThrowException("Ljava/lang/OutOfMemoryError;", "proxy allocation failed");
            return true;
        }
        // `h` is declared by java.lang.reflect.Proxy, which this class extends.
        SetRefField(proxy, "h", "Ljava/lang/reflect/InvocationHandler;", handler);
        // TEMP DIAGNOSTIC (ULTRAKILL dead Runnable proxies): Unity's native bridge
        // keeps the C++ peer for `ptr` and the proxy's interfaces must match what the
        // peer was built for. Log both so a stranded peer can be correlated by ptr.
        // Remove once the failing peer is identified.
        if (DexClass* handler_class = linker->ClassOfObject(handler)) {
            if (handler_class->descriptor != nullptr &&
                std::strstr(handler_class->descriptor, "bitter/jnibridge") != nullptr) {
                std::string names;
                for (size_t i = 0; i < interfaces.size(); ++i) {
                    if (i > 0) names += ",";
                    names += interfaces[i] != nullptr ? interfaces[i]->PrettyName() : "?";
                }
                std::string ptrs;
                for (DexClass* k = handler_class; k != nullptr; k = k->superclass) {
                    for (DexField& f : k->instance_fields) {
                        if (f.type_descriptor != nullptr &&
                            std::strcmp(f.type_descriptor, "J") == 0) {
                            int64_t v =
                                handler->GetField<int64_t>(f.offset_or_slot);
                            char field[48];
                            std::snprintf(field, sizeof(field), " %s=0x%llx",
                                          f.name != nullptr ? f.name : "?",
                                          static_cast<unsigned long long>(v));
                            ptrs += field;
                        }
                    }
                }
                std::fprintf(stderr, "[KuART][PROXY] new jnibridge proxy ifaces=[%s]%s\n",
                             names.c_str(), ptrs.c_str());
            }
        }
        result->l = proxy;
        return true;
    }

    if (std::strcmp(name, "getProxyClass") == 0) {
        std::vector<DexClass*> interfaces;
        if (!collect_interfaces(num_args > 1 ? args[1].l : nullptr, &interfaces)) return true;
        DexClass* proxy_class = linker->GetOrCreateProxyClass(interfaces);
        result->l = proxy_class != nullptr ? linker->GetClassObject(proxy_class) : nullptr;
        return true;
    }

    if (std::strcmp(name, "isProxyClass") == 0) {
        DexClass* k = ClassOf(interp, num_args > 0 ? args[0].l : nullptr);
        *result = DexValue::Int(k != nullptr && k->is_proxy ? 1 : 0);
        return true;
    }

    if (std::strcmp(name, "getInvocationHandler") == 0) {
        DexObject* proxy = num_args > 0 ? args[0].l : nullptr;
        DexClass* k = linker->ClassOfObject(proxy);
        if (k == nullptr || !k->is_proxy) {
            interp->ThrowException("Ljava/lang/IllegalArgumentException;",
                                   "not a proxy instance");
            return true;
        }
        result->l = GetRefField(proxy, "h", "Ljava/lang/reflect/InvocationHandler;");
        return true;
    }

    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// java.lang.invoke — MethodHandle / MethodHandles.Lookup
//
// The narrow reason this exists: an interface DEFAULT method cannot be reached by
// ordinary reflection on a proxy. Method.invoke dispatches virtually, so invoking a
// default on a Proxy instance re-enters the proxy's InvocationHandler and recurses until
// the stack is gone. The platform's answer is Lookup.unreflectSpecial, which yields a
// handle that invokes the method NON-virtually, and that is the whole feature below.
//
// KuART has no bytecode writer, so a handle cannot be a synthesised lambda form. It is a
// resolved DexMethod plus an optional bound receiver plus a "special" flag, and invoking it
// calls that method directly. Composition (filterArguments, asType and friends) is
// therefore absent; direct handles are complete.
// ─────────────────────────────────────────────────────────────────────────────

// A java.lang.invoke.MethodHandle wrapping `m`.
//
// MethodHandle is abstract so that `instanceof MethodHandle` keeps its platform meaning;
// the concrete class is the nested Direct. The type is filled in from the DEX signature
// rather than from whatever the caller asked for, so type() describes the method that will
// actually run.
DexObject* NewMethodHandle(Interpreter* interp, DexMethod* m, DexObject* receiver,
                           bool special) {
    DexClassLinker* linker = interp->linker();
    if (linker == nullptr || m == nullptr) return nullptr;

    DexClass* klass = linker->FindClass("Ljava/lang/invoke/MethodHandle$Direct;");
    if (klass == nullptr) {
        interp->ThrowException("Ljava/lang/InternalError;",
                               "java.lang.invoke.MethodHandle$Direct is missing from the framework");
        return nullptr;
    }
    interp->EnsureInitialized(klass);
    DexObject* handle = linker->AllocObject(klass);
    if (handle == nullptr) {
        interp->ThrowException("Ljava/lang/OutOfMemoryError;", "MethodHandle");
        return nullptr;
    }

    SetLongField(handle, "artMethod", static_cast<int64_t>(reinterpret_cast<uintptr_t>(m)));
    SetRefField(handle, "receiver", "Ljava/lang/Object;", receiver);

    // The two booleans and the type are read back by invokeWithArguments below and by
    // bindTo in Java, so all three must be set even when false — AllocObject zeroes the
    // payload, but relying on that would break the moment a field is reordered.
    if (DexField* f_special = klass->FindInstanceField("special", "Z")) {
        handle->SetField<uint8_t>(f_special->offset_or_slot, special ? 1 : 0);
    }
    if (DexField* f_static = klass->FindInstanceField("isStatic", "Z")) {
        handle->SetField<uint8_t>(f_static->offset_or_slot, m->IsStatic() ? 1 : 0);
    }

    // type() is built by calling MethodType.fromMethodDescriptorString on the method's own
    // signature. Going through the Java factory rather than allocating a MethodType here
    // keeps one implementation of descriptor parsing: a second one in C++ would drift.
    DexClass* mt = linker->FindClass("Ljava/lang/invoke/MethodType;");
    if (mt != nullptr && m->signature != nullptr) {
        interp->EnsureInitialized(mt);
        DexMethod* from_desc = mt->FindDirectMethod(
            "fromMethodDescriptorString",
            "(Ljava/lang/String;Ljava/lang/ClassLoader;)Ljava/lang/invoke/MethodType;");
        if (from_desc != nullptr) {
            DexValue call_args[2];
            call_args[0] = DexValue::Ref(
                reinterpret_cast<DexObject*>(linker->NewString(m->signature)));
            call_args[1] = DexValue::Ref(nullptr);
            const DexValue type_obj = interp->Execute(from_desc, call_args, 2);
            if (interp->HasPendingException()) {
                // A signature this runtime cannot fully describe is not a reason to refuse
                // the handle: the method still resolves and still runs. Drop the type and
                // carry on, so type() reports null rather than the call failing.
                interp->ClearPendingException();
            } else {
                DexMethod* set_type = klass->FindVirtualMethod(
                    "setType", "(Ljava/lang/invoke/MethodType;)V");
                if (set_type != nullptr) {
                    DexValue set_args[2];
                    set_args[0] = DexValue::Ref(handle);
                    set_args[1] = type_obj;
                    interp->Execute(set_type, set_args, 2);
                    if (interp->HasPendingException()) interp->ClearPendingException();
                }
            }
        }
    }
    return handle;
}

// The DEX signature a MethodType describes, or "" when it cannot be read.
//
// Calls toMethodDescriptorString() on the Java object instead of walking its fields: the
// descriptor is what MethodType is authoritative about, and reproducing the conversion here
// would be a second implementation that can disagree with the first.
std::string DescriptorFromMethodType(Interpreter* interp, DexObject* type_obj) {
    if (type_obj == nullptr) return std::string();
    DexClassLinker* linker = interp->linker();
    DexClass* klass = linker != nullptr ? linker->ClassOfObject(type_obj) : nullptr;
    if (klass == nullptr) return std::string();
    DexMethod* to_desc =
        klass->FindVirtualMethod("toMethodDescriptorString", "()Ljava/lang/String;");
    if (to_desc == nullptr) return std::string();
    DexValue arg = DexValue::Ref(type_obj);
    const DexValue str = interp->Execute(to_desc, &arg, 1);
    if (interp->HasPendingException()) {
        interp->ClearPendingException();
        return std::string();
    }
    const char* utf8 = GetStringUtf8(str);
    return utf8 != nullptr ? std::string(utf8) : std::string();
}

// The signature to search for when a Lookup is given a name and a MethodType.
//
// findVirtual's MethodType omits the receiver (it is implied), which already matches a DEX
// signature. findConstructor's names the return type of the constructed object, whereas a
// DEX <init> returns void — hence `force_void_return`.
std::string LookupSignature(Interpreter* interp, DexObject* type_obj, bool force_void_return) {
    std::string desc = DescriptorFromMethodType(interp, type_obj);
    if (desc.empty()) return desc;
    if (!force_void_return) return desc;
    const std::size_t close = desc.find(')');
    if (close == std::string::npos) return desc;
    return desc.substr(0, close + 1) + "V";
}

bool Invoke_java_lang_invoke_MethodHandle(Interpreter* interp, const char* name,
                                          const DexValue* args, size_t num_args,
                                          DexValue* result) {
    if (std::strcmp(name, "invokeWithArguments") != 0) return false;

    DexObject* handle = num_args > 0 ? args[0].l : nullptr;
    if (handle == nullptr) {
        interp->ThrowException("Ljava/lang/NullPointerException;", "null method handle");
        return true;
    }
    DexMethod* target = reinterpret_cast<DexMethod*>(
        static_cast<uintptr_t>(GetLongField(handle, "artMethod")));
    if (target == nullptr) {
        interp->ThrowException("Ljava/lang/IllegalStateException;",
                               "method handle was never resolved");
        return true;
    }

    DexClass* handle_class = interp->linker()->ClassOfObject(handle);
    bool special = false;
    if (handle_class != nullptr) {
        if (DexField* f = handle_class->FindInstanceField("special", "Z")) {
            special = handle->GetField<uint8_t>(f->offset_or_slot) != 0;
        }
    }
    DexObject* bound = GetRefField(handle, "receiver", "Ljava/lang/Object;");
    auto* boxed = num_args > 1 ? reinterpret_cast<DexArray*>(args[1].l) : nullptr;

    // The receiver comes from bindTo when the handle is bound, otherwise from the first
    // element of the argument array — the platform's rule, and the reason the two cases
    // cannot share a single argument-count check.
    DexObject* receiver = bound;
    int32_t first_arg = 0;
    if (!target->IsStatic() && receiver == nullptr) {
        if (boxed == nullptr || boxed->length < 1) {
            interp->ThrowException("Ljava/lang/IllegalArgumentException;",
                                   std::string("no receiver for ") +
                                       (target->name != nullptr ? target->name : "?"));
            return true;
        }
        receiver = reinterpret_cast<DexObject**>(boxed + 1)[0];
        first_arg = 1;
    }
    if (!target->IsStatic() && receiver == nullptr) {
        interp->ThrowException("Ljava/lang/NullPointerException;",
                               std::string("invoke ") +
                                   (target->name != nullptr ? target->name : "?") + " on null");
        return true;
    }

    // Virtual dispatch UNLESS the handle is special. This branch is the entire point of
    // unreflectSpecial: for a default method invoked on a Proxy, re-resolving against the
    // receiver's class would land back on the proxy's bodyless method and be forwarded to
    // the InvocationHandler that is asking for this call — unbounded recursion.
    DexMethod* resolved = target;
    if (!special && !target->IsStatic()) {
        if (DexClass* receiver_class = interp->linker()->ClassOfObject(receiver)) {
            if (DexMethod* found =
                    receiver_class->FindVirtualMethod(target->name, target->signature)) {
                resolved = found;
            }
        }
    }
    if (resolved->IsStatic()) interp->EnsureInitialized(resolved->declaring_class);

    // Unbox into the declared parameter types. The argument array may carry a leading
    // receiver, which BuildInvokeArgs does not expect, so the tail is copied out first.
    const std::vector<std::string> params = SplitParams(resolved->signature);
    const int32_t given = boxed != nullptr ? boxed->length - first_arg : 0;
    if (static_cast<size_t>(given < 0 ? 0 : given) != params.size()) {
        interp->ThrowException("Ljava/lang/IllegalArgumentException;",
                               "wrong argument count for " +
                                   std::string(resolved->name != nullptr ? resolved->name : "?") +
                                   ": expected " + std::to_string(params.size()) + ", got " +
                                   std::to_string(given < 0 ? 0 : given));
        return true;
    }

    std::vector<DexValue> call_args;
    call_args.reserve(params.size() + 1);
    if (!resolved->IsStatic()) call_args.push_back(DexValue::Ref(receiver));
    auto** items = boxed != nullptr ? reinterpret_cast<DexObject**>(boxed + 1) : nullptr;
    for (size_t i = 0; i < params.size(); ++i) {
        DexValue v;
        if (!UnboxValue(interp, params[i].c_str(), items[first_arg + i], &v)) {
            interp->ThrowException("Ljava/lang/IllegalArgumentException;",
                                   "argument " + std::to_string(i) + " is not a " + params[i]);
            return true;
        }
        call_args.push_back(v);
    }

    // A special handle on a method with no body is a genuine error rather than something
    // to forward: the caller asked for THIS method non-virtually and it has no code.
    // Saying so names the method, whereas letting Execute() fall through to proxy dispatch
    // would silently re-enter the handler.
    if (special && resolved->code_item == nullptr && !resolved->IsNative()) {
        interp->ThrowException("Ljava/lang/AbstractMethodError;",
                               (resolved->declaring_class != nullptr
                                    ? resolved->declaring_class->PrettyName() + "."
                                    : std::string()) +
                                   (resolved->name != nullptr ? resolved->name : "?") +
                                   " has no body to invoke directly");
        return true;
    }

    const DexValue ret = interp->Execute(resolved, call_args.data(), call_args.size());
    if (interp->HasPendingException()) return true;

    const char* ret_desc = ReturnDescriptor(resolved);
    result->l = (ret_desc[0] == 'V') ? nullptr : BoxValue(interp, ret_desc, ret);
    return true;
}

bool Invoke_java_lang_invoke_MethodHandles(Interpreter* interp, const char* name,
                                           const DexValue* args, size_t num_args,
                                           DexValue* result) {
    if (std::strcmp(name, "lookup") != 0) return false;

    DexClassLinker* linker = interp->linker();
    DexClass* lookup_class = linker->FindClass("Ljava/lang/invoke/MethodHandles$Lookup;");
    if (lookup_class == nullptr) {
        interp->ThrowException("Ljava/lang/InternalError;",
                               "MethodHandles$Lookup is missing from the framework");
        return true;
    }
    interp->EnsureInitialized(lookup_class);

    // The lookup class is the CALLER's, which the interpreter knows. Reporting a
    // placeholder would break a caller that passes the Lookup to in() or compares
    // lookupClass() against its own — and that comparison is how libraries decide whether
    // they need the reflective Lookup constructor at all.
    DexClass* caller = interp->CallerClass();
    DexObject* caller_class_obj =
        caller != nullptr ? reinterpret_cast<DexObject*>(linker->GetClassObject(caller))
                          : nullptr;

    DexObject* lookup = linker->AllocObject(lookup_class);
    if (lookup == nullptr) {
        interp->ThrowException("Ljava/lang/OutOfMemoryError;", "Lookup");
        return true;
    }
    DexMethod* ctor = lookup_class->FindDirectMethod("<init>", "(Ljava/lang/Class;I)V");
    if (ctor == nullptr) {
        interp->ThrowException("Ljava/lang/InternalError;",
                               "MethodHandles$Lookup(Class,int) is missing");
        return true;
    }
    // PUBLIC|PRIVATE|PROTECTED|PACKAGE: nothing in KuART enforces access, so a narrower
    // mode would only make callers take a fallback path for a restriction that is not real.
    DexValue call_args[3];
    call_args[0] = DexValue::Ref(lookup);
    call_args[1] = DexValue::Ref(caller_class_obj);
    call_args[2] = DexValue::Int(0x0001 | 0x0002 | 0x0004 | 0x0008);
    interp->Execute(ctor, call_args, 3);
    if (interp->HasPendingException()) return true;
    result->l = lookup;
    return true;
}

bool Invoke_java_lang_invoke_Lookup(Interpreter* interp, const char* name,
                                    const DexValue* args, size_t num_args,
                                    DexValue* result) {
    DexClassLinker* linker = interp->linker();

    // unreflect / unreflectSpecial / unreflectConstructor: the Method or Constructor object
    // already carries the resolved DexMethod, so there is nothing to search for.
    const bool is_unreflect = std::strcmp(name, "unreflect") == 0;
    const bool is_unreflect_special = std::strcmp(name, "unreflectSpecial") == 0;
    const bool is_unreflect_ctor = std::strcmp(name, "unreflectConstructor") == 0;
    if (is_unreflect || is_unreflect_special || is_unreflect_ctor) {
        DexObject* member = num_args > 1 ? args[1].l : nullptr;
        if (member == nullptr) {
            interp->ThrowException("Ljava/lang/NullPointerException;",
                                   std::string(name) + " with null member");
            return true;
        }
        DexMethod* m = MethodFromObject(member);
        if (m == nullptr) {
            interp->ThrowException("Ljava/lang/IllegalArgumentException;",
                                   std::string(name) + " argument is not a resolved member");
            return true;
        }
        // A constructor is always invoked non-virtually, and so is unreflectSpecial's
        // result; unreflect() keeps virtual dispatch.
        DexObject* handle =
            NewMethodHandle(interp, m, nullptr, is_unreflect_special || is_unreflect_ctor);
        if (handle == nullptr) return true;
        result->l = handle;
        return true;
    }

    const bool is_find_virtual = std::strcmp(name, "findVirtual") == 0;
    const bool is_find_static = std::strcmp(name, "findStatic") == 0;
    const bool is_find_special = std::strcmp(name, "findSpecial") == 0;
    const bool is_find_ctor = std::strcmp(name, "findConstructor") == 0;
    if (!is_find_virtual && !is_find_static && !is_find_special && !is_find_ctor) {
        return false;
    }

    DexClass* refc = ClassOf(interp, num_args > 1 ? args[1].l : nullptr);
    if (refc == nullptr) {
        interp->ThrowException("Ljava/lang/IllegalArgumentException;",
                               std::string(name) + " with no target class");
        return true;
    }

    std::string member_name;
    DexObject* type_obj = nullptr;
    if (is_find_ctor) {
        member_name = "<init>";
        type_obj = num_args > 2 ? args[2].l : nullptr;
    } else {
        const char* n = num_args > 2 ? GetStringUtf8(args[2]) : nullptr;
        if (n == nullptr) {
            interp->ThrowException("Ljava/lang/NoSuchMethodException;",
                                   refc->PrettyName() + ".<null name>");
            return true;
        }
        member_name = n;
        type_obj = num_args > 3 ? args[3].l : nullptr;
    }

    const std::string signature = LookupSignature(interp, type_obj, is_find_ctor);
    if (signature.empty()) {
        interp->ThrowException("Ljava/lang/IllegalArgumentException;",
                               std::string(name) + " with no usable MethodType");
        return true;
    }

    interp->EnsureInitialized(refc);
    DexMethod* m = nullptr;
    if (is_find_static || is_find_ctor) {
        m = refc->FindDirectMethod(member_name.c_str(), signature.c_str());
    } else {
        m = refc->FindVirtualMethod(member_name.c_str(), signature.c_str());
        // A private or final instance method lives among the direct methods, and both are
        // legitimate findVirtual/findSpecial targets.
        if (m == nullptr) m = refc->FindDirectMethod(member_name.c_str(), signature.c_str());
    }
    if (m == nullptr) {
        // Name the class, member and signature: "no such method" without them cannot be
        // acted on, and the signature is what is usually wrong.
        interp->ThrowException("Ljava/lang/NoSuchMethodException;",
                               refc->PrettyName() + "." + member_name + signature);
        return true;
    }

    DexObject* handle =
        NewMethodHandle(interp, m, nullptr, is_find_special || is_find_ctor);
    if (handle == nullptr) return true;
    result->l = handle;
    return true;
}


bool Invoke_java_lang_System(Interpreter* interp, const char* name, const DexValue* args,
                             size_t /*num_args*/, DexValue* result) {
    if (std::strcmp(name, "currentTimeMillis") == 0) {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::system_clock::now().time_since_epoch())
                      .count();
        *result = DexValue::Long(ms);
        return true;
    }
    if (std::strcmp(name, "nanoTime") == 0) {
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                      std::chrono::steady_clock::now().time_since_epoch())
                      .count();
        *result = DexValue::Long(ns);
        return true;
    }
    if (std::strcmp(name, "identityHashCode") == 0) {
        *result = DexValue::Int(static_cast<int32_t>(reinterpret_cast<uintptr_t>(args[0].l)));
        return true;
    }
    if (std::strcmp(name, "arraycopy") == 0) {
        DexObject* src_obj = args[0].l;
        int32_t src_pos = args[1].i;
        DexObject* dst_obj = args[2].l;
        int32_t dst_pos = args[3].i;
        int32_t length = args[4].i;

        if (src_obj == nullptr || dst_obj == nullptr) {
            interp->ThrowException("Ljava/lang/NullPointerException;", "arraycopy null array");
            return true;
        }
        if (!src_obj->clazz || !src_obj->clazz->is_array ||
            !dst_obj->clazz || !dst_obj->clazz->is_array) {
            interp->ThrowException("Ljava/lang/ArrayStoreException;", "arraycopy non-array argument");
            return true;
        }

        auto* src_arr = reinterpret_cast<DexArray*>(src_obj);
        auto* dst_arr = reinterpret_cast<DexArray*>(dst_obj);

        if (src_pos < 0 || dst_pos < 0 || length < 0 ||
            src_pos + length > src_arr->length ||
            dst_pos + length > dst_arr->length) {
            interp->ThrowException("Ljava/lang/ArrayIndexOutOfBoundsException;", "arraycopy range invalid");
            return true;
        }

        uint32_t elem_size = interp->linker()->ElementSize(src_arr->clazz->component_type);
        uint8_t* src_data = reinterpret_cast<uint8_t*>(src_arr + 1) + src_pos * elem_size;
        uint8_t* dst_data = reinterpret_cast<uint8_t*>(dst_arr + 1) + dst_pos * elem_size;
        std::memmove(dst_data, src_data, static_cast<size_t>(length) * elem_size);
        return true;
    }
    if (std::strcmp(name, "getProperty") == 0) {
        const std::string key = GetStringUtf8(args[0]);
        // Only the properties Android code actually reads; anything else is null
        // so callers fall back to their own defaults.
        const char* value = nullptr;
        if (key == "line.separator") {
            value = "\n";
        } else if (key == "file.separator") {
            value = "/";
        } else if (key == "path.separator") {
            value = ":";
        } else if (key == "java.io.tmpdir") {
            value = "/tmp";
        } else if (key == "user.dir" || key == "user.home") {
            value = "/";
        } else if (key == "java.vm.name") {
            // Libraries decide which platform they are on by comparing this against
            // "Dalvik" — okhttp, guava, gRPC and Kotlin's stdlib all do it. KuDroid
            // reported "KuART", so every one of them took its desktop-JVM branch:
            // okhttp went looking for Conscrypt/BouncyCastle JSSE providers instead
            // of using the Android platform, failed, and left its Platform class
            // permanently in error — which kills all HTTP for the rest of the run.
            //
            // Everything else KuDroid reports is already Android (SDK_INT 29,
            // aarch64), so this is the consistent answer, not a special case.
            value = "Dalvik";
        } else if (key == "java.vm.vendor") {
            value = "The Android Project";
        } else if (key == "java.vendor") {
            value = "The Android Project";
        } else if (key == "java.specification.name") {
            value = "Dalvik Core Library";
        } else if (key == "java.vm.specification.name") {
            value = "Dalvik Virtual Machine Specification";
        } else if (key == "java.vm.version") {
            value = "2.1.0";
        } else if (key == "java.version") {
            value = "0";
        } else if (key == "java.specification.version") {
            value = "0.9";
        } else if (key == "java.runtime.name") {
            value = "Android Runtime";
        } else if (key == "java.runtime.version") {
            value = "0.9";
        } else if (key == "os.name") {
            value = "Linux";
        } else if (key == "os.arch") {
            value = "aarch64";
        } else if (key == "os.version") {
            value = "4.14.0";
        } else if (key == "file.encoding") {
            value = "UTF-8";
        }
        result->l = value != nullptr ? interp->linker()->NewString(value) : nullptr;
        return true;
    }
    if (std::strcmp(name, "getenv") == 0) {
        const std::string key = GetStringUtf8(args[0]);
        const char* value = key.empty() ? nullptr : std::getenv(key.c_str());
        result->l = value != nullptr ? interp->linker()->NewString(value) : nullptr;
        return true;
    }
    if (std::strcmp(name, "exit") == 0) {
        // The guest shares the process with the iOS host, so exiting would kill
        // KuDroid itself. Ignore gracefully.
        std::fprintf(stderr, "[KuART][System] System.exit(%d) called -> ignored to prevent app shutdown\n", args[0].i);
        return true;
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// java.lang.Math
// ─────────────────────────────────────────────────────────────────────────────
bool Invoke_java_lang_Math(Interpreter* /*interp*/, const char* name, const DexValue* args,
                           size_t /*num_args*/, DexValue* result) {
    if (std::strcmp(name, "sin") == 0) { *result = DexValue::Double(std::sin(args[0].d)); return true; }
    if (std::strcmp(name, "cos") == 0) { *result = DexValue::Double(std::cos(args[0].d)); return true; }
    if (std::strcmp(name, "tan") == 0) { *result = DexValue::Double(std::tan(args[0].d)); return true; }
    if (std::strcmp(name, "asin") == 0) { *result = DexValue::Double(std::asin(args[0].d)); return true; }
    if (std::strcmp(name, "acos") == 0) { *result = DexValue::Double(std::acos(args[0].d)); return true; }
    if (std::strcmp(name, "atan") == 0) { *result = DexValue::Double(std::atan(args[0].d)); return true; }
    if (std::strcmp(name, "atan2") == 0) { *result = DexValue::Double(std::atan2(args[0].d, args[1].d)); return true; }
    if (std::strcmp(name, "sinh") == 0) { *result = DexValue::Double(std::sinh(args[0].d)); return true; }
    if (std::strcmp(name, "cosh") == 0) { *result = DexValue::Double(std::cosh(args[0].d)); return true; }
    if (std::strcmp(name, "tanh") == 0) { *result = DexValue::Double(std::tanh(args[0].d)); return true; }
    if (std::strcmp(name, "exp") == 0) { *result = DexValue::Double(std::exp(args[0].d)); return true; }
    if (std::strcmp(name, "log") == 0) { *result = DexValue::Double(std::log(args[0].d)); return true; }
    if (std::strcmp(name, "log10") == 0) { *result = DexValue::Double(std::log10(args[0].d)); return true; }
    if (std::strcmp(name, "sqrt") == 0) { *result = DexValue::Double(std::sqrt(args[0].d)); return true; }
    if (std::strcmp(name, "cbrt") == 0) { *result = DexValue::Double(std::cbrt(args[0].d)); return true; }
    if (std::strcmp(name, "pow") == 0) { *result = DexValue::Double(std::pow(args[0].d, args[1].d)); return true; }
    if (std::strcmp(name, "ceil") == 0) { *result = DexValue::Double(std::ceil(args[0].d)); return true; }
    if (std::strcmp(name, "floor") == 0) { *result = DexValue::Double(std::floor(args[0].d)); return true; }
    if (std::strcmp(name, "rint") == 0) { *result = DexValue::Double(std::rint(args[0].d)); return true; }
    if (std::strcmp(name, "hypot") == 0) { *result = DexValue::Double(std::hypot(args[0].d, args[1].d)); return true; }
    if (std::strcmp(name, "IEEEremainder") == 0) { *result = DexValue::Double(std::remainder(args[0].d, args[1].d)); return true; }
    if (std::strcmp(name, "randomImpl") == 0) {
        static std::mt19937_64 rng(std::random_device{}());
        static std::uniform_real_distribution<double> dist(0.0, 1.0);
        *result = DexValue::Double(dist(rng));
        return true;
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// java.lang.Float / Double bit casts
// ─────────────────────────────────────────────────────────────────────────────

// Double.toString per the Java spec: shortest decimal that round-trips, always
// with a fractional part, scientific notation only outside [1e-3, 1e7).
std::string FormatJavaDouble(double d) {
    if (std::isnan(d)) return "NaN";
    if (std::isinf(d)) return d > 0 ? "Infinity" : "-Infinity";
    if (d == 0.0) return std::signbit(d) ? "-0.0" : "0.0";

    const double magnitude = std::fabs(d);
    const bool scientific = magnitude < 1e-3 || magnitude >= 1e7;

    // %.17g always round-trips; shorter precisions are tried first so 0.1 prints
    // as "0.1" rather than "0.10000000000000001".
    char buf[64];
    for (int precision = 1; precision <= 17; ++precision) {
        std::snprintf(buf, sizeof(buf), scientific ? "%.*E" : "%.*g", precision - 1, d);
        if (std::strtod(buf, nullptr) == d) break;
    }
    std::string out(buf);

    if (!scientific) {
        // Java always shows a decimal point: "1" must become "1.0".
        if (out.find('.') == std::string::npos && out.find('e') == std::string::npos &&
            out.find('E') == std::string::npos) {
            out += ".0";
        }
        return out;
    }

    // Reshape C's "1.5E-05" into Java's "1.5E-5": no leading zeros, mantissa
    // always has a fractional digit.
    const size_t e = out.find('E');
    if (e == std::string::npos) return out;
    std::string mantissa = out.substr(0, e);
    std::string exponent = out.substr(e + 1);
    if (mantissa.find('.') == std::string::npos) mantissa += ".0";

    bool negative_exp = false;
    if (!exponent.empty() && (exponent[0] == '+' || exponent[0] == '-')) {
        negative_exp = exponent[0] == '-';
        exponent.erase(0, 1);
    }
    while (exponent.size() > 1 && exponent[0] == '0') exponent.erase(0, 1);
    return mantissa + "E" + (negative_exp ? "-" : "") + exponent;
}

bool Invoke_java_lang_Float(Interpreter* /*interp*/, const char* name, const DexValue* args,
                            size_t /*num_args*/, DexValue* result) {
    if (std::strcmp(name, "floatToIntBits") == 0 || std::strcmp(name, "floatToRawIntBits") == 0) {
        int32_t bits = 0;
        float f = args[0].f;
        std::memcpy(&bits, &f, sizeof(float));
        *result = DexValue::Int(bits);
        return true;
    }
    if (std::strcmp(name, "intBitsToFloat") == 0) {
        float f = 0.0f;
        int32_t bits = args[0].i;
        std::memcpy(&f, &bits, sizeof(float));
        *result = DexValue::Float(f);
        return true;
    }
    return false;
}

bool Invoke_java_lang_Double(Interpreter* interp, const char* name, const DexValue* args,
                             size_t /*num_args*/, DexValue* result) {
    if (std::strcmp(name, "doubleToLongBits") == 0 || std::strcmp(name, "doubleToRawLongBits") == 0) {
        int64_t bits = 0;
        double d = args[0].d;
        std::memcpy(&bits, &d, sizeof(double));
        *result = DexValue::Long(bits);
        return true;
    }
    if (std::strcmp(name, "longBitsToDouble") == 0) {
        double d = 0.0;
        int64_t bits = args[0].j;
        std::memcpy(&d, &bits, sizeof(double));
        *result = DexValue::Double(d);
        return true;
    }
    if (std::strcmp(name, "toString") == 0) {
        result->l = interp->linker()->NewString(FormatJavaDouble(args[0].d).c_str());
        return true;
    }
    if (std::strcmp(name, "parseDouble") == 0) {
        const char* str = GetStringUtf8(args[0]);
        // Java throws instead of returning 0 for garbage, and accepts the
        // Infinity/NaN spellings that strtod does not.
        while (*str == ' ' || *str == '\t' || *str == '\n' || *str == '\r') ++str;
        std::string text(str);
        while (!text.empty() && (text.back() == ' ' || text.back() == '\t' ||
                                 text.back() == '\n' || text.back() == '\r')) {
            text.pop_back();
        }
        if (!text.empty() && (text.back() == 'f' || text.back() == 'F' ||
                              text.back() == 'd' || text.back() == 'D')) {
            text.pop_back();
        }
        if (text == "NaN") {
            *result = DexValue::Double(std::nan(""));
            return true;
        }
        if (text == "Infinity" || text == "+Infinity") {
            *result = DexValue::Double(HUGE_VAL);
            return true;
        }
        if (text == "-Infinity") {
            *result = DexValue::Double(-HUGE_VAL);
            return true;
        }
        char* end = nullptr;
        const double value = std::strtod(text.c_str(), &end);
        if (text.empty() || end == text.c_str() || *end != '\0') {
            interp->ThrowException("Ljava/lang/NumberFormatException;",
                                   "For input string: \"" + text + "\"");
            return true;
        }
        *result = DexValue::Double(value);
        return true;
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// java.lang.String
// ─────────────────────────────────────────────────────────────────────────────

// Allocates a Java char[] holding `units`.
DexArray* NewCharArray(DexClassLinker* linker, const std::vector<uint16_t>& units) {
    DexClass* klass = linker->FindClass("[C");
    if (klass == nullptr) return nullptr;
    DexArray* arr = linker->AllocArray(klass, static_cast<int32_t>(units.size()));
    if (arr == nullptr) return nullptr;
    auto* data = reinterpret_cast<uint16_t*>(arr + 1);
    for (size_t i = 0; i < units.size(); ++i) data[i] = units[i];
    return arr;
}

DexArray* NewByteArray(DexClassLinker* linker, const char* bytes, size_t count) {
    DexClass* klass = linker->FindClass("[B");
    if (klass == nullptr) return nullptr;
    DexArray* arr = linker->AllocArray(klass, static_cast<int32_t>(count));
    if (arr == nullptr) return nullptr;
    std::memcpy(reinterpret_cast<uint8_t*>(arr + 1), bytes, count);
    return arr;
}

bool Invoke_java_lang_String(Interpreter* interp, const DexMethod* method,
                             const char* name, const DexValue* args, size_t num_args,
                             DexValue* result) {
    const char* signature = method->signature != nullptr ? method->signature : "";
    DexClassLinker* linker = interp->linker();

    // ── constructors: the object exists, this fills in its payload ──
    if (std::strcmp(name, "initEmpty") == 0) {
        InitStringFrom(linker, args[0].l, std::string());
        return true;
    }
    if (std::strcmp(name, "initCopy") == 0) {
        InitStringFrom(linker, args[0].l, GetStringUtf8(args[1]));
        return true;
    }
    if (std::strcmp(name, "initChars") == 0) {
        auto* chars = reinterpret_cast<DexArray*>(args[1].l);
        const int32_t offset = args[2].i;
        const int32_t count = args[3].i;
        if (chars == nullptr) {
            interp->ThrowException("Ljava/lang/NullPointerException;", "char[] null");
            return true;
        }
        if (offset < 0 || count < 0 || offset + count > chars->length) {
            interp->ThrowException("Ljava/lang/StringIndexOutOfBoundsException;",
                                   "offset " + std::to_string(offset) + " count " +
                                       std::to_string(count));
            return true;
        }
        const auto* data = reinterpret_cast<const uint16_t*>(chars + 1);
        InitStringFrom(linker, args[0].l,
                       Utf16ToUtf8(data + offset, static_cast<size_t>(count)));
        return true;
    }
    if (std::strcmp(name, "initBytes") == 0) {
        auto* bytes = reinterpret_cast<DexArray*>(args[1].l);
        const int32_t offset = args[2].i;
        const int32_t count = args[3].i;
        if (bytes == nullptr) {
            interp->ThrowException("Ljava/lang/NullPointerException;", "byte[] null");
            return true;
        }
        if (offset < 0 || count < 0 || offset + count > bytes->length) {
            interp->ThrowException("Ljava/lang/StringIndexOutOfBoundsException;",
                                   "offset " + std::to_string(offset));
            return true;
        }
        // Bytes are taken as UTF-8 directly; KuART stores strings as UTF-8 so no
        // charset decoding step is needed for the only encoding Android uses.
        const auto* data = reinterpret_cast<const char*>(bytes + 1);
        InitStringFrom(linker, args[0].l,
                       std::string(data + offset, static_cast<size_t>(count)));
        return true;
    }

    DexString* self = AsString(args[0]);
    if (self == nullptr) {
        interp->ThrowException("Ljava/lang/NullPointerException;", "null string receiver");
        return true;
    }

    if (std::strcmp(name, "intern") == 0) {
        result->l = linker->InternString(self->utf8 ? self->utf8 : "");
        return true;
    }
    if (std::strcmp(name, "length") == 0) {
        *result = DexValue::Int(self->ascii ? static_cast<int32_t>(self->length)
                                           : static_cast<int32_t>(Utf16Length(
                                                 self->utf8, self->length)));
        return true;
    }

    Utf16View view(self);

    if (std::strcmp(name, "charAt") == 0) {
        const int32_t index = args[1].i;
        if (index < 0 || static_cast<uint32_t>(index) >= view.length()) {
            interp->ThrowException("Ljava/lang/StringIndexOutOfBoundsException;",
                                   "charAt " + std::to_string(index));
            return true;
        }
        *result = DexValue::Int(view.at(static_cast<uint32_t>(index)));
        return true;
    }
    if (std::strcmp(name, "indexOf") == 0 || std::strcmp(name, "lastIndexOf") == 0) {
        const bool last = name[0] == 'l';
        // Two overloads share the name: (int ch, int from) and (String, int from).
        // Only the signature can tell them apart — an int register holding 'l'
        // looks exactly like a reference.
        const bool by_string = std::strncmp(signature, "(Ljava/lang/String;", 19) == 0;
        const int32_t from = num_args > 2 ? args[2].i : 0;
        const int32_t len = static_cast<int32_t>(view.length());

        if (!by_string) {
            const auto needle = static_cast<uint16_t>(args[1].i);
            int32_t found = -1;
            if (last) {
                for (int32_t i = std::min(from, len - 1); i >= 0; --i) {
                    if (view.at(static_cast<uint32_t>(i)) == needle) {
                        found = i;
                        break;
                    }
                }
            } else {
                for (int32_t i = std::max(from, 0); i < len; ++i) {
                    if (view.at(static_cast<uint32_t>(i)) == needle) {
                        found = i;
                        break;
                    }
                }
            }
            *result = DexValue::Int(found);
            return true;
        }

        Utf16View needle(AsString(args[1]));
        const int32_t nlen = static_cast<int32_t>(needle.length());
        int32_t found = -1;
        if (nlen == 0) {
            found = last ? std::min(from, len) : std::min(std::max(from, 0), len);
        } else if (last) {
            for (int32_t i = std::min(from, len - nlen); i >= 0; --i) {
                bool match = true;
                for (int32_t k = 0; k < nlen; ++k) {
                    if (view.at(static_cast<uint32_t>(i + k)) !=
                        needle.at(static_cast<uint32_t>(k))) {
                        match = false;
                        break;
                    }
                }
                if (match) {
                    found = i;
                    break;
                }
            }
        } else {
            for (int32_t i = std::max(from, 0); i + nlen <= len; ++i) {
                bool match = true;
                for (int32_t k = 0; k < nlen; ++k) {
                    if (view.at(static_cast<uint32_t>(i + k)) !=
                        needle.at(static_cast<uint32_t>(k))) {
                        match = false;
                        break;
                    }
                }
                if (match) {
                    found = i;
                    break;
                }
            }
        }
        *result = DexValue::Int(found);
        return true;
    }
    if (std::strcmp(name, "substring") == 0) {
        const int32_t begin = args[1].i;
        const int32_t end = args[2].i;
        const int32_t len = static_cast<int32_t>(view.length());
        if (begin < 0 || end > len || begin > end) {
            interp->ThrowException("Ljava/lang/StringIndexOutOfBoundsException;",
                                   "substring(" + std::to_string(begin) + ", " +
                                       std::to_string(end) + ") len " +
                                       std::to_string(len));
            return true;
        }
        result->l = linker->NewString(
            view.SubstringUtf8(static_cast<uint32_t>(begin), static_cast<uint32_t>(end))
                .c_str());
        return true;
    }
    if (std::strcmp(name, "concat") == 0) {
        std::string out(self->utf8 ? self->utf8 : "", self->length);
        out += GetStringUtf8(args[1]);
        result->l = linker->NewString(out.c_str());
        return true;
    }
    if (std::strcmp(name, "replace") == 0) {
        const bool by_char = std::strncmp(signature, "(CC)", 4) == 0;
        if (by_char) {
            const auto from = static_cast<uint16_t>(args[1].i);
            const auto to = static_cast<uint16_t>(args[2].i);
            std::vector<uint16_t> units;
            units.reserve(view.length());
            for (uint32_t i = 0; i < view.length(); ++i) {
                const uint16_t c = view.at(i);
                units.push_back(c == from ? to : c);
            }
            result->l = linker->NewString(Utf16ToUtf8(units.data(), units.size()).c_str());
            return true;
        }
        const std::string haystack(self->utf8 ? self->utf8 : "", self->length);
        const std::string target = GetStringUtf8(args[1]);
        const std::string replacement = GetStringUtf8(args[2]);
        if (target.empty()) {
            result->l = linker->NewString(haystack.c_str());
            return true;
        }
        std::string out;
        size_t pos = 0;
        while (true) {
            const size_t hit = haystack.find(target, pos);
            if (hit == std::string::npos) {
                out.append(haystack, pos, std::string::npos);
                break;
            }
            out.append(haystack, pos, hit - pos);
            out += replacement;
            pos = hit + target.size();
        }
        result->l = linker->NewString(out.c_str());
        return true;
    }
    if (std::strcmp(name, "toLowerCase") == 0 || std::strcmp(name, "toUpperCase") == 0) {
        // ASCII-only: full Unicode case mapping needs ICU tables that KuDroid
        // does not ship.
        const bool upper = name[2] == 'U';
        std::vector<uint16_t> units;
        units.reserve(view.length());
        for (uint32_t i = 0; i < view.length(); ++i) {
            uint16_t c = view.at(i);
            if (upper && c >= 'a' && c <= 'z') {
                c = static_cast<uint16_t>(c - 32);
            } else if (!upper && c >= 'A' && c <= 'Z') {
                c = static_cast<uint16_t>(c + 32);
            }
            units.push_back(c);
        }
        result->l = linker->NewString(Utf16ToUtf8(units.data(), units.size()).c_str());
        return true;
    }
    if (std::strcmp(name, "trim") == 0) {
        // Java trims every code unit <= ' ', not just the usual whitespace set.
        uint32_t begin = 0;
        uint32_t end = view.length();
        while (begin < end && view.at(begin) <= ' ') ++begin;
        while (end > begin && view.at(end - 1) <= ' ') --end;
        result->l = linker->NewString(view.SubstringUtf8(begin, end).c_str());
        return true;
    }
    if (std::strcmp(name, "equalsIgnoreCase") == 0) {
        DexString* other = AsString(args[1]);
        if (other == nullptr) {
            *result = DexValue::Int(0);
            return true;
        }
        Utf16View rhs(other);
        if (view.length() != rhs.length()) {
            *result = DexValue::Int(0);
            return true;
        }
        bool equal = true;
        for (uint32_t i = 0; i < view.length(); ++i) {
            uint16_t a = view.at(i);
            uint16_t b = rhs.at(i);
            if (a >= 'A' && a <= 'Z') a = static_cast<uint16_t>(a + 32);
            if (b >= 'A' && b <= 'Z') b = static_cast<uint16_t>(b + 32);
            if (a != b) {
                equal = false;
                break;
            }
        }
        *result = DexValue::Int(equal ? 1 : 0);
        return true;
    }
    if (std::strcmp(name, "compareTo") == 0) {
        DexString* other = AsString(args[1]);
        if (other == nullptr) {
            interp->ThrowException("Ljava/lang/NullPointerException;", "compareTo null");
            return true;
        }
        Utf16View rhs(other);
        const uint32_t shared = std::min(view.length(), rhs.length());
        int32_t diff = 0;
        for (uint32_t i = 0; i < shared; ++i) {
            diff = static_cast<int32_t>(view.at(i)) - static_cast<int32_t>(rhs.at(i));
            if (diff != 0) break;
        }
        if (diff == 0) {
            diff = static_cast<int32_t>(view.length()) - static_cast<int32_t>(rhs.length());
        }
        *result = DexValue::Int(diff);
        return true;
    }
    if (std::strcmp(name, "getBytes") == 0) {
        result->l = NewByteArray(linker, self->utf8 ? self->utf8 : "", self->length);
        return true;
    }
    if (std::strcmp(name, "toCharArray") == 0) {
        std::vector<uint16_t> units;
        units.reserve(view.length());
        for (uint32_t i = 0; i < view.length(); ++i) units.push_back(view.at(i));
        result->l = NewCharArray(linker, units);
        return true;
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// java.lang.Thread
// ─────────────────────────────────────────────────────────────────────────────

// Thread that is currently running bytecode. Set on entry to a Java thread body
// so currentThread() can return the right object without a thread-id map.
thread_local DexObject* t_current_thread = nullptr;

// The main thread has no java.lang.Thread object until something asks for one.
DexObject* MainThreadObject(Interpreter* interp) {
    static DexObject* main_thread = nullptr;
    if (main_thread == nullptr) {
        DexClass* klass = interp->linker()->FindClass("Ljava/lang/Thread;");
        if (klass == nullptr) return nullptr;
        interp->EnsureInitialized(klass);
        main_thread = interp->linker()->AllocObject(klass);
        if (main_thread != nullptr) {
            SetRefField(main_thread, "name", "Ljava/lang/String;",
                        interp->linker()->NewString("main"));
        }
    }
    return main_thread;
}

bool Invoke_java_lang_Thread(Interpreter* interp, const char* name, const DexValue* args,
                             size_t num_args, DexValue* result) {
    if (std::strcmp(name, "currentThread") == 0) {
        result->l = t_current_thread != nullptr ? t_current_thread : MainThreadObject(interp);
        return true;
    }
    if (std::strcmp(name, "sleep") == 0) {
        const int64_t ms = args[0].j;
        const int32_t nanos = num_args > 1 ? args[1].i : 0;
        if (ms > 0 || nanos > 0) {
            // Sleeping keeps the VM lock released so other Java threads can run.
            VmLockRelease unlocked;
            std::this_thread::sleep_for(std::chrono::milliseconds(ms) +
                                        std::chrono::nanoseconds(nanos));
        }
        return true;
    }
    if (std::strcmp(name, "yield") == 0) {
        VmLockRelease unlocked;
        std::this_thread::yield();
        return true;
    }
    if (std::strcmp(name, "nativeStart") == 0) {
        DexObject* self = args[0].l;
        if (self == nullptr || self->clazz == nullptr) {
            interp->ThrowException("Ljava/lang/NullPointerException;", "thread null");
            return true;
        }
        DexMethod* body = self->clazz->FindDirectMethod("runFromNative", "()V");
        if (body == nullptr) body = self->clazz->FindVirtualMethod("runFromNative", "()V");
        if (body == nullptr) {
            interp->ThrowException("Ljava/lang/NoSuchMethodError;", "runFromNative");
            return true;
        }

        std::thread worker([interp, self, body]() {
            t_current_thread = self;
            DexValue arg = DexValue::Ref(self);
            // Execute takes the VM lock itself at depth 0.
            interp->Execute(body, &arg, 1);
            interp->ClearPendingException();
        });
        SetLongField(self, "nativePeer",
                     static_cast<int64_t>(std::hash<std::thread::id>{}(worker.get_id())));
        worker.detach();
        return true;
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// java.io.File
// ─────────────────────────────────────────────────────────────────────────────

// The path lives in the `path` field written by File's Java constructor.
std::string GetFilePath(const DexValue& file_obj) {
    if (file_obj.l == nullptr) return "";
    DexClass* klass = file_obj.l->clazz;
    if (!klass) return "";
    DexField* f_path = klass->FindInstanceField("path", "Ljava/lang/String;");
    if (!f_path) f_path = klass->FindInstanceField("mPath", "Ljava/lang/String;");
    if (!f_path) return "";
    DexObject* path_str_obj = file_obj.l->GetField<DexObject*>(f_path->offset_or_slot);
    if (!path_str_obj) return "";
    auto* str = reinterpret_cast<DexString*>(path_str_obj);
    return str->utf8 ? str->utf8 : "";
}

// Where a Java path actually lands on disk.
//
// A guest's own native code reaches the filesystem through the vfs_* shims, which rewrite
// "/data/data/<pkg>/files" into the app's container. Java did not: everything below called
// bare open/stat/mkdir, so getFilesDir() aimed at the REAL /data/data — a path iOS does not
// let anyone write. mkdirs() failed, and File.mkdirs() swallows the failure, so nothing
// reported it and every Java write went nowhere.
//
// Routing through the same remapper as native code is also what keeps the two consistent: a
// file the guest writes with fopen() must be the file Java reads with FileInputStream.
std::string RemapJavaPath(const std::string& path) {
    if (path.empty()) return path;
    return VFSPathRemapper::getInstance().remap(path.c_str());
}

bool Invoke_java_io_File(Interpreter* interp, const char* name, const DexValue* args,
                         size_t num_args, DexValue* result) {
    std::string path = RemapJavaPath(GetFilePath(args[0]));
    if (path.empty()) {
        // Every native here returns boolean/long/array; zero is the right answer
        // for an unusable path in all of them.
        *result = DexValue::Int(0);
        if (std::strcmp(name, "list") == 0) result->l = nullptr;
        return true;
    }

    struct stat st;
    int res = stat(path.c_str(), &st);

    if (std::strcmp(name, "exists") == 0) {
        *result = DexValue::Int(res == 0 ? 1 : 0);
        return true;
    }
    if (std::strcmp(name, "isDirectory") == 0) {
        *result = DexValue::Int(res == 0 && S_ISDIR(st.st_mode) ? 1 : 0);
        return true;
    }
    if (std::strcmp(name, "isFile") == 0) {
        *result = DexValue::Int(res == 0 && S_ISREG(st.st_mode) ? 1 : 0);
        return true;
    }
    if (std::strcmp(name, "renameTo") == 0) {
        // The destination needs the same remapping as the source, or a rename inside
        // /data/data would move a container file to a path outside it and fail.
        const std::string dest = RemapJavaPath(GetFilePath(num_args > 1 ? args[1] : DexValue()));
        *result = DexValue::Int(
            (!dest.empty() && std::rename(path.c_str(), dest.c_str()) == 0) ? 1 : 0);
        return true;
    }
    if (std::strcmp(name, "list") == 0) {
        DIR* dir = opendir(path.c_str());
        if (dir == nullptr) {
            result->l = nullptr;
            return true;
        }
        std::vector<DexObject*> names;
        while (dirent* entry = readdir(dir)) {
            if (std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            names.push_back(interp->linker()->NewString(entry->d_name));
        }
        closedir(dir);
        result->l = NewRefArray(interp->linker(), "[Ljava/lang/String;", names);
        return true;
    }
    if (std::strcmp(name, "canRead") == 0) {
        *result = DexValue::Int(access(path.c_str(), R_OK) == 0 ? 1 : 0);
        return true;
    }
    if (std::strcmp(name, "canWrite") == 0) {
        *result = DexValue::Int(access(path.c_str(), W_OK) == 0 ? 1 : 0);
        return true;
    }
    if (std::strcmp(name, "length") == 0) {
        *result = DexValue::Long(res == 0 ? st.st_size : 0);
        return true;
    }
    if (std::strcmp(name, "lastModified") == 0) {
        *result = DexValue::Long(res == 0 ? static_cast<int64_t>(st.st_mtime) * 1000 : 0);
        return true;
    }
    if (std::strcmp(name, "delete") == 0) {
        *result = DexValue::Int((unlink(path.c_str()) == 0 || rmdir(path.c_str()) == 0) ? 1 : 0);
        return true;
    }
    if (std::strcmp(name, "mkdir") == 0) {
        *result = DexValue::Int(mkdir(path.c_str(), 0755) == 0 ? 1 : 0);
        return true;
    }
    if (std::strcmp(name, "createNewFile") == 0) {
        int fd = open(path.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0644);
        if (fd >= 0) {
            close(fd);
            *result = DexValue::Int(1);
        } else {
            *result = DexValue::Int(0);
        }
        return true;
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// java.io.FileInputStream / FileOutputStream / PrintStream
// ─────────────────────────────────────────────────────────────────────────────
bool Invoke_java_io_FileInputStream(Interpreter* /*interp*/, const char* name, const DexValue* args,
                                    size_t /*num_args*/, DexValue* result) {
    if (std::strcmp(name, "openNative") == 0) {
        // Static native: args[0] is the path, not a receiver.
        const std::string path = RemapJavaPath(GetStringUtf8(args[0]));
        int fd = path.empty() ? -1 : open(path.c_str(), O_RDONLY);
        *result = DexValue::Int(fd);
        return true;
    }
    if (std::strcmp(name, "readNative") == 0) {
        int fd = args[0].i;
        auto* arr = reinterpret_cast<DexArray*>(args[1].l);
        int32_t off = args[2].i;
        int32_t len = args[3].i;
        if (fd < 0 || arr == nullptr || off < 0 || len < 0 || off + len > arr->length) {
            *result = DexValue::Int(-1);
            return true;
        }
        uint8_t* buf = reinterpret_cast<uint8_t*>(arr + 1) + off;
        ssize_t n = read(fd, buf, static_cast<size_t>(len));
        *result = DexValue::Int(static_cast<int32_t>(n));
        return true;
    }
    if (std::strcmp(name, "skipNative") == 0) {
        const int fd = args[0].i;
        const int64_t n = args[1].j;
        if (fd < 0 || n <= 0) {
            *result = DexValue::Long(0);
            return true;
        }
        const off_t before = lseek(fd, 0, SEEK_CUR);
        const off_t after = lseek(fd, static_cast<off_t>(n), SEEK_CUR);
        *result = DexValue::Long(after < 0 || before < 0 ? 0 : static_cast<int64_t>(after - before));
        return true;
    }
    if (std::strcmp(name, "availableNative") == 0) {
        const int fd = args[0].i;
        struct stat st;
        const off_t pos = fd >= 0 ? lseek(fd, 0, SEEK_CUR) : -1;
        if (fd < 0 || pos < 0 || fstat(fd, &st) != 0) {
            *result = DexValue::Int(0);
            return true;
        }
        const off_t remaining = st.st_size - pos;
        *result = DexValue::Int(remaining > 0 ? static_cast<int32_t>(remaining) : 0);
        return true;
    }
    if (std::strcmp(name, "closeNative") == 0) {
        int fd = args[0].i;
        if (fd >= 0) close(fd);
        return true;
    }
    return false;
}

bool Invoke_java_io_FileOutputStream(Interpreter* /*interp*/, const char* name, const DexValue* args,
                                     size_t /*num_args*/, DexValue* result) {
    if (std::strcmp(name, "openNative") == 0) {
        const std::string path = RemapJavaPath(GetStringUtf8(args[0]));
        bool append = args[1].i != 0;
        int flags = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);
        int fd = path.empty() ? -1 : open(path.c_str(), flags, 0644);
        *result = DexValue::Int(fd);
        return true;
    }
    if (std::strcmp(name, "writeNative") == 0) {
        int fd = args[0].i;
        auto* arr = reinterpret_cast<DexArray*>(args[1].l);
        int32_t off = args[2].i;
        int32_t len = args[3].i;
        if (fd >= 0 && arr != nullptr && off >= 0 && len >= 0 && off + len <= arr->length) {
            uint8_t* buf = reinterpret_cast<uint8_t*>(arr + 1) + off;
            ssize_t n = write(fd, buf, static_cast<size_t>(len));
            *result = DexValue::Int(static_cast<int32_t>(n));
        } else {
            *result = DexValue::Int(-1);
        }
        return true;
    }
    if (std::strcmp(name, "closeNative") == 0) {
        int fd = args[0].i;
        if (fd >= 0) close(fd);
        return true;
    }
    return false;
}

bool Invoke_java_io_PrintStream(Interpreter* /*interp*/, const char* name, const DexValue* args,
                                size_t /*num_args*/, DexValue* /*result*/) {
    if (std::strcmp(name, "writeNative") == 0) {
        int fd = args[0].i;
        auto* arr = reinterpret_cast<DexArray*>(args[1].l);
        if (arr != nullptr) {
            uint8_t* buf = reinterpret_cast<uint8_t*>(arr + 1);
            write(fd == 2 ? STDERR_FILENO : STDOUT_FILENO, buf, static_cast<size_t>(arr->length));
        }
        return true;
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// android.* native methods
// ─────────────────────────────────────────────────────────────────────────────
bool Invoke_android_util_Log(Interpreter* /*interp*/, const char* name, const DexValue* args,
                             size_t /*num_args*/, DexValue* result) {
    if (std::strcmp(name, "println_native") == 0) {
        int priority = args[0].i;
        const char* tag = GetStringUtf8(args[1]);
        const char* msg = GetStringUtf8(args[2]);
        const char* prio_char = "V";
        if (priority == 3) prio_char = "D";
        else if (priority == 4) prio_char = "I";
        else if (priority == 5) prio_char = "W";
        else if (priority == 6) prio_char = "E";
        else if (priority == 7) prio_char = "A";

        std::fprintf(stderr, "[%s/%s] %s\n", prio_char, tag ? tag : "", msg ? msg : "");
        *result = DexValue::Int(0);
        return true;
    }
    return false;
}

bool Invoke_android_graphics_Canvas(Interpreter* /*interp*/, const char* name, const DexValue* args,
                                    size_t /*num_args*/, DexValue* result) {
    if (std::strcmp(name, "native_drawColor") == 0) {
        JavaCanvasRenderer::getInstance().drawColor(static_cast<uint32_t>(args[0].i));
        return true;
    }
    if (std::strcmp(name, "native_drawRect") == 0) {
        JavaCanvasRenderer::getInstance().drawRect(
            args[0].f, args[1].f, args[2].f, args[3].f,
            static_cast<uint32_t>(args[4].i));
        return true;
    }
    if (std::strcmp(name, "native_drawText") == 0) {
        const char* text = GetStringUtf8(args[0]);
        JavaCanvasRenderer::getInstance().drawText(
            text, args[1].f, args[2].f, static_cast<uint32_t>(args[3].i),
            args[4].f);
        return true;
    }
    if (std::strcmp(name, "native_drawBitmap") == 0) {
        auto* arr = reinterpret_cast<DexArray*>(args[0].l);
        if (arr != nullptr) {
            auto* pixels = reinterpret_cast<uint32_t*>(arr + 1);
            JavaCanvasRenderer::getInstance().drawBitmap(
                pixels, args[1].i, args[2].i, args[3].f, args[4].f);
        }
        return true;
    }
    if (std::strcmp(name, "native_flush") == 0) {
        JavaCanvasRenderer::getInstance().flush();
        return true;
    }
    // Real surface size. Canvas used to hardcode 1080x1920, so on any other screen
    // the layout was computed for the wrong extent and drawing fell off the edge
    // (an 828x1792 device lost everything past ~77% of the width).
    if (std::strcmp(name, "native_getSurfaceWidth") == 0) {
        *result = DexValue::Int(JavaCanvasRenderer::getInstance().getWidth());
        return true;
    }
    if (std::strcmp(name, "native_getSurfaceHeight") == 0) {
        *result = DexValue::Int(JavaCanvasRenderer::getInstance().getHeight());
        return true;
    }
    return false;
}

bool Invoke_android_app_Activity(Interpreter* /*interp*/, const char* name, const DexValue* args,
                                 size_t num_args, DexValue* /*result*/) {
    if (std::strcmp(name, "setRequestedOrientation_native") == 0) {
        kudroid_set_requested_orientation(args[0].i);
        return true;
    }
    if (std::strcmp(name, "nativeRequestPermissions") == 0) {
        DexObject* act = num_args > 0 ? args[0].l : nullptr;
        const char* csv = num_args > 1 ? GetStringUtf8(args[1]) : "";
        int reqCode = num_args > 2 ? args[2].i : 0;
        kudroid_prompt_permission_request("", csv, reqCode, act);
        return true;
    }
    return false;
}

bool Invoke_android_os_Vibrator(Interpreter* /*interp*/, const char* name, const DexValue* args,
                                size_t /*num_args*/, DexValue* /*result*/) {
    if (std::strcmp(name, "kudroid_vibrate_native") == 0) {
        kudroid_vibrate(args[0].i);
        return true;
    }
    return false;}

// ─────────────────────────────────────────────────────────────────────────────
// android.view.Choreographer — the JAVA frame-callback API.
//
// The NDK half (AChoreographer_*) was implemented first and ULTRAKILL still stopped,
// because Unity uses both and this class was a nine-line stub with no methods:
//
//     android.view.Choreographer->getInstance()Landroid/view/Choreographer;
//     android.view.Choreographer->postFrameCallback(...FrameCallback;)V
//     android.view.Choreographer->postFrameCallbackDelayed(...FrameCallback;J)V
//
// The class existed, so Interpreter::ResolveMethod auto-stubbed the missing methods
// into bodyless DexMethods rather than reporting them. getInstance() returned null,
// and Unity's JNIBridge responded by throwing NoSuchMethodError itself — the string
// "JNIBridge error: Java interface default methods are only supported since Android
// Oreo" sits in ULTRAKILL's own classes.dex right beside Ljava/lang/NoSuchMethodError;.
// That went uncaught out of Looper.loop, ActivityThread.main returned, and the shell
// printed "Session ended" while FMOD's audio thread carried on.
//
// Frames come from the SAME pacer the NDK entry points use. Two frame sources in one
// process would mean two clocks: a guest posting on both would read timestamps that
// disagree and pace against whichever it saw last.
//
// The FrameCallback object is registered as a JNI global reference before being
// queued. Without that it is a bare DexObject* held only by the pacer — and although
// KuART's heap never frees, the object must still be reachable for the interpreter to
// dispatch on it, and a global ref is what states that intent rather than relying on
// the absence of a collector.
namespace {

// A queued Java frame callback. Allocated per post and owned by the pacer until the
// callback fires or is removed.
struct JavaFrameCallback {
    Interpreter* interp;
    DexObject* callback;
};

// Every JavaFrameCallback handed to the pacer, so removeFrameCallback can find the
// context belonging to a given Java object and the entry can be freed exactly once.
std::mutex g_java_frame_mutex;
std::vector<JavaFrameCallback*> g_java_frame_contexts;

// Invoked by the pacer — either on the guest thread that polled its looper, or on the
// pacer thread. Runs bytecode, so it must take the VM lock, which Execute() does for
// itself when the calling thread does not already hold it.
void JavaFrameCallbackTrampoline(int64_t frame_time_ns, void* data) {
    auto* ctx = static_cast<JavaFrameCallback*>(data);
    if (ctx == nullptr) return;

    Interpreter* interp = ctx->interp;
    DexObject* callback = ctx->callback;
    {
        std::lock_guard<std::mutex> lock(g_java_frame_mutex);
        for (size_t i = 0; i < g_java_frame_contexts.size(); ++i) {
            if (g_java_frame_contexts[i] == ctx) {
                g_java_frame_contexts.erase(g_java_frame_contexts.begin() +
                                            static_cast<long>(i));
                break;
            }
        }
    }

    if (interp != nullptr && callback != nullptr && interp->linker() != nullptr) {
        DexClass* klass = interp->linker()->ClassOfObject(callback);
        if (klass != nullptr) {
            // Virtual dispatch on the callback's real class. The interface method is
            // doFrame(J)V; a Proxy implementing FrameCallback resolves through the
            // proxy path inside Execute, which is why this looks the method up on the
            // receiver rather than on the interface.
            DexMethod* do_frame = klass->FindVirtualMethod("doFrame", "(J)V");
            if (do_frame != nullptr) {
                const DexValue call_args[3] = {DexValue::Ref(callback),
                                               DexValue::Long(frame_time_ns), DexValue()};
                interp->ClearPendingException();
                interp->Execute(do_frame, call_args, 2);
                // An exception out of a frame callback is the app's own, and there is
                // no Java frame above this to catch it. Clearing rather than
                // propagating: the alternative is to leave it pending on a thread that
                // is about to run unrelated work, which would surface it at whatever
                // the next Java call happens to be.
                if (interp->HasPendingException()) {
                    std::fprintf(stderr,
                                 "[KuART][Choreographer] exception in doFrame: %s\n",
                                 interp->last_error().c_str());
                    interp->ClearPendingException();
                }
            }
        }
    }
    delete ctx;
}

}  // namespace

bool Invoke_android_view_Choreographer(Interpreter* interp, const char* name,
                                       const DexValue* args, size_t num_args,
                                       DexValue* result) {
    if (std::strcmp(name, "nativePostFrameCallback") == 0) {
        // (this, FrameCallback, long delayMillis)
        if (num_args < 3) return false;
        DexObject* callback = args[1].l;
        if (callback == nullptr) {
            interp->ThrowException("Ljava/lang/IllegalArgumentException;",
                                   "postFrameCallback with a null callback");
            return true;
        }
        // Keep the callback reachable for as long as the pacer holds it. KuART's heap
        // has no collector, but a global ref is the statement of intent — and it is
        // what keeps this correct if one is ever added.
        if (interp->jni_env() != nullptr) {
            interp->jni_env()->AddGlobalRef(callback);
        }
        auto* ctx = new JavaFrameCallback{interp, callback};
        {
            std::lock_guard<std::mutex> lock(g_java_frame_mutex);
            g_java_frame_contexts.push_back(ctx);
        }
        const int64_t delay_ms = args[2].j;
        kudroid::frame_pacer_post(
            kudroid::frame_pacer_instance_for_this_thread(),
            reinterpret_cast<void*>(&JavaFrameCallbackTrampoline), ctx,
            delay_ms > 0 ? static_cast<uint64_t>(delay_ms) * 1000000ull : 0);
        return true;
    }
    if (std::strcmp(name, "nativeRemoveFrameCallback") == 0) {
        if (num_args < 2) return false;
        DexObject* callback = args[1].l;
        if (callback == nullptr) return true;
        // Every context naming this Java object, because the same callback posted
        // twice is queued twice and Android's remove clears both.
        std::vector<JavaFrameCallback*> matches;
        {
            std::lock_guard<std::mutex> lock(g_java_frame_mutex);
            for (size_t i = g_java_frame_contexts.size(); i-- > 0;) {
                if (g_java_frame_contexts[i]->callback == callback) {
                    matches.push_back(g_java_frame_contexts[i]);
                    g_java_frame_contexts.erase(g_java_frame_contexts.begin() +
                                                static_cast<long>(i));
                }
            }
        }
        for (JavaFrameCallback* ctx : matches) {
            // Only free the context once the pacer has agreed it no longer holds it.
            // A context the pacer had already dequeued is about to be freed by the
            // trampoline, and deleting it here as well would be a double free.
            if (kudroid::frame_pacer_remove(
                    reinterpret_cast<void*>(&JavaFrameCallbackTrampoline), ctx) > 0) {
                delete ctx;
            }
        }
        return true;
    }
    if (std::strcmp(name, "nativeGetFrameIntervalNanos") == 0) {
        *result = DexValue::Long(kudroid::frame_pacer_interval_ns());
        return true;
    }
    if (std::strcmp(name, "nativeGetFrameTimeNanos") == 0) {
        // Inside a frame callback this is that frame's timestamp; outside one there is
        // no frame, and "now" is more useful than an exception or a stale value. Both
        // are on CLOCK_MONOTONIC, the clock System.nanoTime reports.
        const int64_t in_frame = kudroid::frame_pacer_current_frame_time_ns();
        if (in_frame != 0) {
            *result = DexValue::Long(in_frame);
            return true;
        }
        struct timespec ts {};
        ::clock_gettime(CLOCK_MONOTONIC, &ts);
        *result = DexValue::Long(static_cast<int64_t>(ts.tv_sec) * 1000000000ll +
                                 static_cast<int64_t>(ts.tv_nsec));
        return true;
    }
    return false;
}

bool Invoke_keep_screen_on(Interpreter* /*interp*/, const char* name, const DexValue* args,
                           size_t /*num_args*/, DexValue* /*result*/) {
    if (std::strcmp(name, "setKeepScreenOnNative") == 0) {
        kudroid_set_keep_screen_on(args[0].i != 0 ? 1 : 0);
        return true;
    }
    return false;
}

// android.media.AudioTrack -> the host audio queue.
//
// This is the JAVA audio path, and it is the one that matters for Unity: FMOD drives
// AudioTrack from Java rather than calling OpenSL ES or AAudio from native code. The
// framework stub had a single empty constructor, so every method was auto-stubbed to
// return 0 and FMOD read that as a device it could not open:
//
//     [KuART][MISSING-METHOD] Auto-stubbing: AudioTrack->getMinBufferSize(III)I
//     [E/FMOD] AudioTrack failed to initialize (status 0)
//
// It then retried forever — the thread sampler caught the loop spinning in malloc with lr
// in DexClassLinker::FindClass, CPU climbing across five samples ten seconds apart.
// ULTRAKILL reached Vulkan and its swapchain and never produced a frame.
//
// Every method here forwards to AudioShim, which already owns a real CoreAudio queue on
// iOS. Same output, same callback, same frame counter as the other two audio APIs.
bool Invoke_android_media_AudioTrack(Interpreter* interp, const char* name,
                                     const DexValue* args, size_t num_args,
                                     DexValue* result) {
    // Static: (sampleRate, channelCount, encoding). No receiver, so args start at 0.
    if (std::strcmp(name, "nativeGetMinBufferSize") == 0) {
        if (num_args < 3) return false;
        *result = DexValue::Int(bionic_kudroid_audiotrack_min_buffer_size(
            args[0].i, args[1].i, args[2].i));
        return true;
    }
    if (std::strcmp(name, "nativeCreate") == 0) {
        if (num_args < 4) return false;
        *result = DexValue::Long(bionic_kudroid_audiotrack_create(
            args[0].i, args[1].i, args[2].i, args[3].i));
        return true;
    }

    // The rest take the handle first. A zero handle is a released or never-opened track
    // and the shim answers ERROR_INVALID_OPERATION for it rather than dereferencing.
    if (std::strcmp(name, "nativeWrite") == 0) {
        // (track, byte[] data, offsetInBytes, sizeInBytes)
        if (num_args < 4) return false;
        auto* data = reinterpret_cast<DexArray*>(args[1].l);
        const int32_t offset = args[2].i;
        const int32_t size = args[3].i;
        if (data == nullptr) {
            *result = DexValue::Int(-3);  // ERROR_INVALID_OPERATION
            return true;
        }
        // Bounds are re-checked here even though AudioTrack.java checks them: the Java
        // side can be bypassed by a guest calling the native method through reflection,
        // and this reads raw memory.
        if (offset < 0 || size < 0 || offset > data->length ||
            size > data->length - offset) {
            *result = DexValue::Int(-2);  // ERROR_BAD_VALUE
            return true;
        }
        *result = DexValue::Int(bionic_kudroid_audiotrack_write(
            args[0].j, data->Data() + offset, size));
        return true;
    }
    if (std::strcmp(name, "nativeWriteShorts") == 0) {
        // (track, short[] data, offsetInShorts, sizeInShorts) — offsets are in ELEMENTS,
        // and PCM 16-bit is what FMOD writes, so this is the hot path.
        if (num_args < 4) return false;
        auto* data = reinterpret_cast<DexArray*>(args[1].l);
        const int32_t offset = args[2].i;
        const int32_t count = args[3].i;
        if (data == nullptr) {
            *result = DexValue::Int(-3);
            return true;
        }
        if (offset < 0 || count < 0 || offset > data->length ||
            count > data->length - offset) {
            *result = DexValue::Int(-2);
            return true;
        }
        const int32_t written = bionic_kudroid_audiotrack_write(
            args[0].j, data->Data() + static_cast<size_t>(offset) * sizeof(int16_t),
            count * static_cast<int32_t>(sizeof(int16_t)));
        // Back to elements, because that is what the Java caller passed and expects.
        *result = DexValue::Int(written < 0 ? written
                                            : written / static_cast<int32_t>(sizeof(int16_t)));
        return true;
    }
    if (std::strcmp(name, "nativePlay") == 0) {
        if (num_args < 1) return false;
        *result = DexValue::Int(bionic_kudroid_audiotrack_play(args[0].j));
        return true;
    }
    if (std::strcmp(name, "nativePause") == 0) {
        if (num_args < 1) return false;
        *result = DexValue::Int(bionic_kudroid_audiotrack_pause(args[0].j));
        return true;
    }
    if (std::strcmp(name, "nativeStop") == 0) {
        if (num_args < 1) return false;
        *result = DexValue::Int(bionic_kudroid_audiotrack_stop(args[0].j));
        return true;
    }
    if (std::strcmp(name, "nativeFlush") == 0) {
        if (num_args < 1) return false;
        *result = DexValue::Int(bionic_kudroid_audiotrack_flush(args[0].j));
        return true;
    }
    if (std::strcmp(name, "nativeRelease") == 0) {
        if (num_args < 1) return false;
        *result = DexValue::Int(bionic_kudroid_audiotrack_release(args[0].j));
        return true;
    }
    if (std::strcmp(name, "nativeSetVolume") == 0) {
        if (num_args < 2) return false;
        *result = DexValue::Int(bionic_kudroid_audiotrack_set_volume(args[0].j, args[1].f));
        return true;
    }
    if (std::strcmp(name, "nativeGetPlaybackHeadPosition") == 0) {
        if (num_args < 1) return false;
        *result = DexValue::Int(bionic_kudroid_audiotrack_head_position(args[0].j));
        return true;
    }
    if (std::strcmp(name, "nativeGetLatencyFrames") == 0) {
        if (num_args < 1) return false;
        *result = DexValue::Int(bionic_kudroid_audiotrack_latency_frames(args[0].j));
        return true;
    }
    (void)interp;
    return false;
}

// InputMethodManager -> host keyboard.
//
// KuDroid ships no keyboard, so the guest's request is forwarded to whatever the host
// registered (on iOS, a view that becomes first responder). The return value tells the
// guest only whether a host was listening; InputMethodManager reports success to the
// app either way, because an app told the keyboard cannot be shown disables its own
// text entry rather than retrying.
bool Invoke_android_view_inputmethod_InputMethodManager(Interpreter* /*interp*/,
                                                        const char* name,
                                                        const DexValue* args,
                                                        size_t num_args,
                                                        DexValue* result) {
    if (std::strcmp(name, "showSoftInputNative") == 0) {
        const int flags = num_args > 0 ? args[0].i : 0;
        *result = DexValue::Int(kudroid_show_soft_input(flags) != 0 ? 1 : 0);
        return true;
    }
    if (std::strcmp(name, "hideSoftInputNative") == 0) {
        *result = DexValue::Int(kudroid_hide_soft_input() != 0 ? 1 : 0);
        return true;
    }
    if (std::strcmp(name, "isSoftInputVisibleNative") == 0) {
        *result = DexValue::Int(kudroid_is_soft_input_visible() != 0 ? 1 : 0);
        return true;
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Device memory, read from the host.
//
// Apps size caches, texture atlases and world chunks from these numbers. Fixed
// values are worse than they look: too high and the app allocates past what the
// device can give and gets killed mid-load; too low and it runs degraded on
// hardware that could do better. Reading the host makes the answer correct on
// whatever device KuDroid happens to be on, with no per-app knowledge.
bool Invoke_kudroid_memory(Interpreter* /*interp*/, const char* name,
                           const DexValue* /*args*/, size_t /*num_args*/,
                           DexValue* result) {
    if (std::strcmp(name, "nativeTotalMemory") == 0) {
        *result = DexValue::Long(
            static_cast<int64_t>(kudroid::query_system_memory().total_bytes));
        return true;
    }
    if (std::strcmp(name, "nativeAvailableMemory") == 0) {
        const kudroid::SystemMemory mem = kudroid::query_system_memory();
        // Report the per-process headroom when the OS enforces one. On iOS jetsam
        // kills a process well below system-available memory, so handing back the
        // system figure invites an app to allocate its way into a kill.
        const uint64_t value = mem.process_available_bytes > 0
                                   ? mem.process_available_bytes
                                   : mem.available_bytes;
        *result = DexValue::Long(static_cast<int64_t>(value));
        return true;
    }
    if (std::strcmp(name, "nativeSystemAvailableMemory") == 0) {
        *result = DexValue::Long(
            static_cast<int64_t>(kudroid::query_system_memory().available_bytes));
        return true;
    }
    if (std::strcmp(name, "nativeProcessResidentMemory") == 0) {
        *result = DexValue::Long(
            static_cast<int64_t>(kudroid::query_system_memory().process_resident_bytes));
        return true;
    }
    if (std::strcmp(name, "nativeIsLowMemory") == 0) {
        *result = DexValue::Int(kudroid::query_system_memory().low_memory ? 1 : 0);
        return true;
    }
    if (std::strcmp(name, "nativeMemoryClass") == 0) {
        *result = DexValue::Int(kudroid::memory_class_mb());
        return true;
    }
    if (std::strcmp(name, "nativeLargeMemoryClass") == 0) {
        *result = DexValue::Int(kudroid::large_memory_class_mb());
        return true;
    }

    // java.lang.Runtime heap figures.
    //
    // KuART has no separate managed heap — objects come from the process allocator —
    // so the process budget IS the heap budget. Kept consistent so that
    // total + free == max, which is what callers assume when they compute headroom.
    if (std::strcmp(name, "maxMemory") == 0) {
        const kudroid::SystemMemory mem = kudroid::query_system_memory();
        const uint64_t ceiling = mem.process_available_bytes > 0
                                     ? mem.process_resident_bytes + mem.process_available_bytes
                                     : mem.total_bytes;
        *result = DexValue::Long(static_cast<int64_t>(ceiling));
        return true;
    }
    if (std::strcmp(name, "totalMemory") == 0) {
        const kudroid::SystemMemory mem = kudroid::query_system_memory();
        // What the process has taken so far.
        uint64_t taken = mem.process_resident_bytes;
        if (taken == 0) taken = mem.total_bytes - mem.available_bytes;
        *result = DexValue::Long(static_cast<int64_t>(taken));
        return true;
    }
    if (std::strcmp(name, "freeMemory") == 0) {
        const kudroid::SystemMemory mem = kudroid::query_system_memory();
        const uint64_t free_bytes = mem.process_available_bytes > 0
                                        ? mem.process_available_bytes
                                        : mem.available_bytes;
        *result = DexValue::Long(static_cast<int64_t>(free_bytes));
        return true;
    }
    if (std::strcmp(name, "availableProcessors") == 0) {
        // Thread-pool sizes are derived from this, and it must agree with what native
        // code sees through sysconf and /proc/cpuinfo: a guest that sizes a Java pool
        // from Runtime and a native pool from sysconf would otherwise build two pools
        // for two different machines. Same source for both now.
        *result = DexValue::Int(
            static_cast<int32_t>(kudroid::query_cpu_topology().total_cores));
        return true;
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// java.lang.Runtime
static LoadLibraryCallback g_load_lib_cb = nullptr;

// ─────────────────────────────────────────────────────────────────────────────
bool Invoke_java_lang_Runtime(Interpreter* interp, const char* name, const DexValue* args,
                              size_t num_args, DexValue* /*result*/) {
    const bool by_name = std::strcmp(name, "loadLibrary") == 0;
    if (!by_name && std::strcmp(name, "load") != 0) return false;

    const char* arg = num_args > 1 ? GetStringUtf8(args[1]) : "";
    if (arg == nullptr || arg[0] == '\0') {
        interp->ThrowException("Ljava/lang/UnsatisfiedLinkError;", "empty library name");
        return true;
    }
    if (g_load_lib_cb != nullptr) {
        if (!g_load_lib_cb(arg)) {
            interp->ThrowException("Ljava/lang/UnsatisfiedLinkError;",
                                   std::string("Couldn't load ") + arg);
            return true;
        }
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// dalvik.system.BaseDexClassLoader
// ─────────────────────────────────────────────────────────────────────────────
bool Invoke_dalvik_system_BaseDexClassLoader(Interpreter* interp, const char* name,
                                            const DexValue* args, size_t num_args,
                                            DexValue* result) {
    if (std::strcmp(name, "findLibraryPath") != 0) return false;

    // Static method, so args[0] is the library name rather than a receiver.
    const char* lib = num_args > 0 ? GetStringUtf8(args[0]) : "";
    result->l = nullptr;
    if (lib == nullptr || lib[0] == '\0') return true;

    // PATH_MAX would do, but the buffer is the failure mode this API is most likely
    // to hit on iOS: a container path is ~110 characters before the app's own
    // /data/app/<pkg>/lib/arm64-v8a/lib<name>.so is appended.
    char path[2048];
    if (kudroid_find_native_library(lib, path, sizeof(path)) != 0) {
        result->l = reinterpret_cast<DexObject*>(interp->linker()->NewString(path));
    }
    return true;
}

}  // namespace

void LibCoreSetLoadLibraryCallback(LoadLibraryCallback cb) {
    g_load_lib_cb = cb;
}
bool Invoke_sun_misc_Unsafe(Interpreter* /*interp*/, const char* name, const DexValue* args,
                            size_t num_args, DexValue* result) {
    if (std::strcmp(name, "objectFieldOffset") == 0) {
        DexObject* field_obj = num_args > 1 ? args[1].l : nullptr;
        if (!field_obj) {
            *result = DexValue::Long(0);
            return true;
        }
        *result = DexValue::Long(reinterpret_cast<uintptr_t>(field_obj));
        return true;
    }
    if (std::strcmp(name, "arrayBaseOffset") == 0) {
        *result = DexValue::Int(0);
        return true;
    }
    if (std::strcmp(name, "arrayIndexScale") == 0) {
        *result = DexValue::Int(1);
        return true;
    }
    if (std::strcmp(name, "compareAndSwapInt") == 0) {
        DexObject* obj = num_args > 1 ? args[1].l : nullptr;
        uint64_t offset = num_args > 2 ? static_cast<uint64_t>(args[2].j) : 0;
        int32_t expected = num_args > 3 ? args[3].i : 0;
        int32_t new_val = num_args > 4 ? args[4].i : 0;
        if (obj == nullptr) {
            *result = DexValue::Int(0);
            return true;
        }
        if (obj->clazz && offset < obj->clazz->object_size) {
            auto* target = reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(obj) + offset);
            bool success = __atomic_compare_exchange_n(target, &expected, new_val, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
            *result = DexValue::Int(success ? 1 : 0);
            return true;
        }
        *result = DexValue::Int(1);
        return true;
    }
    if (std::strcmp(name, "compareAndSwapLong") == 0) {
        DexObject* obj = num_args > 1 ? args[1].l : nullptr;
        uint64_t offset = num_args > 2 ? static_cast<uint64_t>(args[2].j) : 0;
        int64_t expected = num_args > 3 ? args[3].j : 0;
        int64_t new_val = num_args > 4 ? args[4].j : 0;
        if (obj == nullptr) {
            *result = DexValue::Int(0);
            return true;
        }
        if (obj->clazz && offset < obj->clazz->object_size) {
            auto* target = reinterpret_cast<int64_t*>(reinterpret_cast<uint8_t*>(obj) + offset);
            bool success = __atomic_compare_exchange_n(target, &expected, new_val, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
            *result = DexValue::Int(success ? 1 : 0);
            return true;
        }
        *result = DexValue::Int(1);
        return true;
    }
    if (std::strcmp(name, "compareAndSwapObject") == 0) {
        DexObject* obj = num_args > 1 ? args[1].l : nullptr;
        uint64_t offset = num_args > 2 ? static_cast<uint64_t>(args[2].j) : 0;
        DexObject* expected = num_args > 3 ? args[3].l : nullptr;
        DexObject* new_val = num_args > 4 ? args[4].l : nullptr;
        if (obj == nullptr) {
            *result = DexValue::Int(0);
            return true;
        }
        if (obj->clazz && offset < obj->clazz->object_size) {
            auto* target = reinterpret_cast<DexObject**>(reinterpret_cast<uint8_t*>(obj) + offset);
            bool success = __atomic_compare_exchange_n(target, &expected, new_val, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
            *result = DexValue::Int(success ? 1 : 0);
            return true;
        }
        *result = DexValue::Int(1);
        return true;
    }
    if (std::strcmp(name, "getInt") == 0 || std::strcmp(name, "getIntVolatile") == 0) {
        DexObject* obj = num_args > 1 ? args[1].l : nullptr;
        uint64_t offset = num_args > 2 ? static_cast<uint64_t>(args[2].j) : 0;
        if (obj && obj->clazz && offset < obj->clazz->object_size) {
            int32_t val = *reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(obj) + offset);
            *result = DexValue::Int(val);
        } else {
            *result = DexValue::Int(0);
        }
        return true;
    }
    if (std::strcmp(name, "putInt") == 0 || std::strcmp(name, "putIntVolatile") == 0 ||
        std::strcmp(name, "putOrderedInt") == 0) {
        DexObject* obj = num_args > 1 ? args[1].l : nullptr;
        uint64_t offset = num_args > 2 ? static_cast<uint64_t>(args[2].j) : 0;
        int32_t val = num_args > 3 ? args[3].i : 0;
        if (obj && obj->clazz && offset < obj->clazz->object_size) {
            *reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(obj) + offset) = val;
        }
        return true;
    }
    if (std::strcmp(name, "getLong") == 0 || std::strcmp(name, "getLongVolatile") == 0) {
        DexObject* obj = num_args > 1 ? args[1].l : nullptr;
        uint64_t offset = num_args > 2 ? static_cast<uint64_t>(args[2].j) : 0;
        if (obj && obj->clazz && offset < obj->clazz->object_size) {
            int64_t val = *reinterpret_cast<int64_t*>(reinterpret_cast<uint8_t*>(obj) + offset);
            *result = DexValue::Long(val);
        } else {
            *result = DexValue::Long(0);
        }
        return true;
    }
    if (std::strcmp(name, "putLong") == 0 || std::strcmp(name, "putLongVolatile") == 0 ||
        std::strcmp(name, "putOrderedLong") == 0) {
        DexObject* obj = num_args > 1 ? args[1].l : nullptr;
        uint64_t offset = num_args > 2 ? static_cast<uint64_t>(args[2].j) : 0;
        int64_t val = num_args > 3 ? args[3].j : 0;
        if (obj && obj->clazz && offset < obj->clazz->object_size) {
            *reinterpret_cast<int64_t*>(reinterpret_cast<uint8_t*>(obj) + offset) = val;
        }
        return true;
    }
    if (std::strcmp(name, "getObject") == 0 || std::strcmp(name, "getObjectVolatile") == 0) {
        DexObject* obj = num_args > 1 ? args[1].l : nullptr;
        uint64_t offset = num_args > 2 ? static_cast<uint64_t>(args[2].j) : 0;
        if (obj && obj->clazz && offset < obj->clazz->object_size) {
            DexObject* val = *reinterpret_cast<DexObject**>(reinterpret_cast<uint8_t*>(obj) + offset);
            *result = DexValue::Ref(val);
        } else {
            *result = DexValue::Ref(nullptr);
        }
        return true;
    }
    if (std::strcmp(name, "putObject") == 0 || std::strcmp(name, "putObjectVolatile") == 0 ||
        std::strcmp(name, "putOrderedObject") == 0) {
        DexObject* obj = num_args > 1 ? args[1].l : nullptr;
        uint64_t offset = num_args > 2 ? static_cast<uint64_t>(args[2].j) : 0;
        DexObject* val = num_args > 3 ? args[3].l : nullptr;
        if (obj && obj->clazz && offset < obj->clazz->object_size) {
            *reinterpret_cast<DexObject**>(reinterpret_cast<uint8_t*>(obj) + offset) = val;
        }
        return true;
    }
    if (std::strcmp(name, "park") == 0 || std::strcmp(name, "unpark") == 0) {
        return true;
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// java.util.TimeZone
//
// There is no zoneinfo database on the device side of KuDroid, so the default zone
// comes from the host's own UTC offset via localtime(). Enough for formatting and
// for the getDefault() call apps make at startup.
bool Invoke_java_util_TimeZone(Interpreter* interp, const char* name,
                               const DexValue* /*args*/, size_t /*num_args*/,
                               DexValue* result) {
    if (std::strcmp(name, "native_getDefaultRawOffset") == 0) {
        const std::time_t now = std::time(nullptr);
        std::tm local_tm{};
        std::tm utc_tm{};
#if defined(_WIN32)
        localtime_s(&local_tm, &now);
        gmtime_s(&utc_tm, &now);
#else
        localtime_r(&now, &local_tm);
        gmtime_r(&now, &utc_tm);
#endif
        // Difference in seconds between local wall clock and UTC for this instant.
        // Computed via mktime on both so day/month rollover is handled.
        local_tm.tm_isdst = 0;
        utc_tm.tm_isdst = 0;
        const double diff = std::difftime(std::mktime(&local_tm), std::mktime(&utc_tm));
        *result = DexValue::Int(static_cast<int32_t>(diff * 1000.0));
        return true;
    }
    if (std::strcmp(name, "native_getDefaultId") == 0) {
        const std::time_t now = std::time(nullptr);
        std::tm local_tm{};
#if defined(_WIN32)
        localtime_s(&local_tm, &now);
#else
        localtime_r(&now, &local_tm);
#endif
        char zone[64] = {0};
        if (std::strftime(zone, sizeof(zone), "%Z", &local_tm) == 0 || zone[0] == '\0') {
            std::snprintf(zone, sizeof(zone), "UTC");
        }
        // NewString copies into the KuART heap, which owns the result.
        result->l = (interp != nullptr && interp->linker() != nullptr)
                        ? reinterpret_cast<DexObject*>(interp->linker()->NewString(zone))
                        : nullptr;
        return true;
    }
    return false;
}

bool LibCoreInvoke(Interpreter* interp, const DexMethod* method, const DexValue* args,
                   size_t num_args, DexValue* result) {
    if (method == nullptr || method->declaring_class == nullptr) return false;
    const char* desc = method->declaring_class->descriptor;
    const char* name = method->name;

    if (std::strcmp(desc, "Ljava/lang/Object;") == 0) return Invoke_java_lang_Object(interp, name, args, num_args, result);
    if (std::strcmp(desc, "Ljava/lang/Class;") == 0) return Invoke_java_lang_Class(interp, name, args, num_args, result);
    if (std::strcmp(desc, "Ljava/lang/System;") == 0) return Invoke_java_lang_System(interp, name, args, num_args, result);
    if (std::strcmp(desc, "Ljava/lang/Math;") == 0) return Invoke_java_lang_Math(interp, name, args, num_args, result);
    if (std::strcmp(desc, "Ljava/lang/Float;") == 0) return Invoke_java_lang_Float(interp, name, args, num_args, result);
    if (std::strcmp(desc, "Ljava/lang/Double;") == 0) return Invoke_java_lang_Double(interp, name, args, num_args, result);
    if (std::strcmp(desc, "Ljava/lang/String;") == 0) return Invoke_java_lang_String(interp, method, name, args, num_args, result);
    if (std::strcmp(desc, "Ljava/lang/Thread;") == 0) return Invoke_java_lang_Thread(interp, name, args, num_args, result);
    if (std::strcmp(desc, "Ljava/lang/Runtime;") == 0) {
        // Runtime owns both loadLibrary/load and the heap figures; memory first so
        // totalMemory/freeMemory report the device rather than a constant.
        if (Invoke_kudroid_memory(interp, name, args, num_args, result)) return true;
        return Invoke_java_lang_Runtime(interp, name, args, num_args, result);
    }
    if (std::strcmp(desc, "Ljava/lang/reflect/Method;") == 0) return Invoke_java_lang_reflect_Method(interp, name, args, num_args, result);
    if (std::strcmp(desc, "Ljava/lang/reflect/Constructor;") == 0) return Invoke_java_lang_reflect_Constructor(interp, name, args, num_args, result);
    if (std::strcmp(desc, "Ljava/lang/reflect/Field;") == 0) return Invoke_java_lang_reflect_Field(interp, name, args, num_args, result);
    if (std::strcmp(desc, "Ljava/lang/reflect/Array;") == 0) return Invoke_java_lang_reflect_Array(interp, method, name, args, num_args, result);
    if (std::strcmp(desc, "Ljava/lang/reflect/Proxy;") == 0) return Invoke_java_lang_reflect_Proxy(interp, name, args, num_args, result);
    // java.lang.invoke. MethodHandle is abstract and the concrete class is its nested
    // Direct, so both descriptors must route here — a handle created by the linker is a
    // Direct, and that is the class the interpreter sees on the receiver.
    if (std::strcmp(desc, "Ljava/lang/invoke/MethodHandle;") == 0 ||
        std::strcmp(desc, "Ljava/lang/invoke/MethodHandle$Direct;") == 0) {
        return Invoke_java_lang_invoke_MethodHandle(interp, name, args, num_args, result);
    }
    if (std::strcmp(desc, "Ljava/lang/invoke/MethodHandles;") == 0) {
        return Invoke_java_lang_invoke_MethodHandles(interp, name, args, num_args, result);
    }
    if (std::strcmp(desc, "Ljava/lang/invoke/MethodHandles$Lookup;") == 0) {
        return Invoke_java_lang_invoke_Lookup(interp, name, args, num_args, result);
    }
    if (std::strcmp(desc, "Ljava/io/File;") == 0) return Invoke_java_io_File(interp, name, args, num_args, result);
    if (std::strcmp(desc, "Ljava/io/FileInputStream;") == 0) return Invoke_java_io_FileInputStream(interp, name, args, num_args, result);
    if (std::strcmp(desc, "Ljava/io/FileOutputStream;") == 0) return Invoke_java_io_FileOutputStream(interp, name, args, num_args, result);
    if (std::strcmp(desc, "Ljava/io/PrintStream;") == 0) return Invoke_java_io_PrintStream(interp, name, args, num_args, result);
    if (std::strcmp(desc, "Ljava/util/TimeZone;") == 0) return Invoke_java_util_TimeZone(interp, name, args, num_args, result);
    if (std::strcmp(desc, "Lsun/misc/Unsafe;") == 0) return Invoke_sun_misc_Unsafe(interp, name, args, num_args, result);
    if (std::strcmp(desc, "Ldalvik/system/BaseDexClassLoader;") == 0) {
        return Invoke_dalvik_system_BaseDexClassLoader(interp, name, args, num_args, result);
    }

    if (std::strcmp(desc, "Landroid/util/Log;") == 0) return Invoke_android_util_Log(interp, name, args, num_args, result);
    if (std::strcmp(desc, "Landroid/graphics/Canvas;") == 0) return Invoke_android_graphics_Canvas(interp, name, args, num_args, result);
    if (std::strcmp(desc, "Landroid/app/Activity;") == 0) return Invoke_android_app_Activity(interp, name, args, num_args, result);
    // The JAVA frame-callback API, sharing one frame source with the NDK
    // AChoreographer_* entry points. Absent, it was auto-stubbed to return null and
    // Unity's JNIBridge threw NoSuchMethodError out of Looper.loop.
    if (std::strcmp(desc, "Landroid/view/Choreographer;") == 0) {
        return Invoke_android_view_Choreographer(interp, name, args, num_args, result);
    }
    if (std::strcmp(desc, "Landroid/os/Vibrator;") == 0) return Invoke_android_os_Vibrator(interp, name, args, num_args, result);
    // AudioTrack is the Java audio path FMOD uses. Without it every method was
    // auto-stubbed to 0 and FMOD retried "AudioTrack failed to initialize" forever.
    if (std::strcmp(desc, "Landroid/media/AudioTrack;") == 0) {
        return Invoke_android_media_AudioTrack(interp, name, args, num_args, result);
    }
    // Memory figures come from the host device rather than constants: apps size
    // caches from them, so a wrong value either gets the process killed or makes it
    // run degraded.
    if (std::strcmp(desc, "Landroid/app/ActivityManager;") == 0 ||
        std::strcmp(desc, "Landroid/os/Debug;") == 0) {
        return Invoke_kudroid_memory(interp, name, args, num_args, result);
    }
    if (std::strcmp(desc, "Landroid/view/Window;") == 0 ||
        std::strcmp(desc, "Landroid/view/View;") == 0 ||
        std::strcmp(desc, "Landroid/os/PowerManager$WakeLock;") == 0) {
        return Invoke_keep_screen_on(interp, name, args, num_args, result);
    }
    if (std::strcmp(desc, "Landroid/view/inputmethod/InputMethodManager;") == 0) {
        return Invoke_android_view_inputmethod_InputMethodManager(interp, name, args,
                                                                 num_args, result);
    }

    return false;
}

bool LibCoreHasMethod(const DexMethod* method) {
    if (method == nullptr || method->declaring_class == nullptr) return false;
    const char* desc = method->declaring_class->descriptor;
    if (desc == nullptr) return false;

    return (std::strncmp(desc, "Ljava/", 6) == 0 ||
            std::strcmp(desc, "Lsun/misc/Unsafe;") == 0 ||
            std::strcmp(desc, "Ldalvik/system/BaseDexClassLoader;") == 0 ||
            std::strcmp(desc, "Landroid/util/Log;") == 0 ||
            std::strcmp(desc, "Landroid/graphics/Canvas;") == 0 ||
            std::strcmp(desc, "Landroid/app/Activity;") == 0 ||
            std::strcmp(desc, "Landroid/app/ActivityManager;") == 0 ||
            std::strcmp(desc, "Landroid/os/Debug;") == 0 ||
            std::strcmp(desc, "Landroid/os/Vibrator;") == 0 ||
            std::strcmp(desc, "Landroid/media/AudioTrack;") == 0 ||
            std::strcmp(desc, "Landroid/view/Window;") == 0 ||
            std::strcmp(desc, "Landroid/view/View;") == 0 ||
            std::strcmp(desc, "Landroid/view/inputmethod/InputMethodManager;") == 0 ||
            std::strcmp(desc, "Landroid/os/PowerManager$WakeLock;") == 0);
}

// ── Bridges for the interpreter's proxy dispatch ──────────────────────────────
//
// Thin forwards to the anonymous-namespace helpers above. The alternative was
// making those helpers public, which would widen this file's surface for one caller.

DexObject* LibCoreGetRefField(DexObject* obj, const char* name, const char* type) {
    return GetRefField(obj, name, type);
}

DexObject* LibCoreNewMethodObject(DexClassLinker* linker, DexMethod* m) {
    return NewMethodObject(linker, m);
}

DexArray* LibCoreNewRefArray(DexClassLinker* linker, const char* array_descriptor,
                             const std::vector<DexObject*>& items) {
    return NewRefArray(linker, array_descriptor, items);
}

std::vector<std::string> LibCoreSplitParams(const char* signature) {
    return SplitParams(signature);
}

DexObject* LibCoreBoxValue(Interpreter* interp, const char* descriptor, const DexValue& v) {
    return BoxValue(interp, descriptor, v);
}

bool LibCoreUnboxValue(Interpreter* interp, const char* descriptor, DexObject* obj,
                       DexValue* out) {
    return UnboxValue(interp, descriptor, obj, out);
}

}  // namespace kuart
}  // namespace kudroid
