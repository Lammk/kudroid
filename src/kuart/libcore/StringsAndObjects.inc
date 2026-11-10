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
        if (str->clazz != nullptr && str->clazz->descriptor != nullptr &&
            std::strcmp(str->clazz->descriptor, "Ljava/lang/String;") != 0) {
            return;
        }
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
    DexString* fresh = linker->NewString(utf8);
    if (fresh == nullptr) return;
    str->utf8 = fresh->utf8;
    str->length = fresh->length;
    str->ascii = fresh->ascii;
}

// java.lang.Object
DexObject* CurrentThreadObject(Interpreter* interp);
bool ThreadIsInterrupted(DexObject* thread);
bool ClearThreadInterrupted(DexObject* thread);

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
        const int32_t nanos = num_args > 2 ? args[2].i : 0;
        if (millis < 0 || nanos < 0 || nanos > 999999) {
            interp->ThrowException("Ljava/lang/IllegalArgumentException;", "invalid wait timeout");
            return true;
        }
        if (!Monitor::IsOwned(self)) {
            interp->ThrowException("Ljava/lang/IllegalMonitorStateException;", "wait without owning the monitor");
            return true;
        }
        DexObject* thread = CurrentThreadObject(interp);
        if (ClearThreadInterrupted(thread)) {
            interp->ThrowException("Ljava/lang/InterruptedException;", "wait interrupted");
            return true;
        }
        if (!Monitor::Wait(self, millis, nanos, thread)) {
            interp->ThrowException("Ljava/lang/IllegalMonitorStateException;", "wait without owning the monitor");
        } else if (ClearThreadInterrupted(thread)) {
            interp->ThrowException("Ljava/lang/InterruptedException;", "wait interrupted");
        }
        return true;
    }
    return false;
}

// reflection object plumbing

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

int32_t GetIntField(DexObject* obj, const char* name, int32_t fallback) {
    if (obj == nullptr || obj->clazz == nullptr) return fallback;
    DexField* f = obj->clazz->FindInstanceField(name, "I");
    return f != nullptr ? obj->GetField<int32_t>(f->offset_or_slot) : fallback;
}

// Maps java.lang.Thread priority to a host scheduling class for the calling thread.
// Only raises: a normal-priority thread keeps the default class.
void ApplyHostThreadPriority(int32_t javaPriority) {
#if defined(__APPLE__)
    if (javaPriority >= 10) {
        pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    } else if (javaPriority >= 7) {
        pthread_set_qos_class_self_np(QOS_CLASS_USER_INITIATED, 0);
    }
#else
    (void)javaPriority;
#endif
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

// Resolve a class from a Class instance, raw jclass, or null if neither.
DexClass* ClassOf(Interpreter* interp, DexObject* class_object) {
    if (class_object == nullptr) return nullptr;
    DexClassLinker* linker = interp->linker();
    if (linker == nullptr) return nullptr;

    // A java.lang.Class instance: the registered mapping is authoritative.
    if (DexClass* k = linker->ClassFromObject(class_object)) return k;

    // A raw jclass. Checked against the linker, never assumed - an unregistered pointer
    // that happened to be passed here must not be treated as class metadata.
    if (linker->IsRegisteredClass(reinterpret_cast<const DexClass*>(class_object))) {
        return reinterpret_cast<DexClass*>(class_object);
    }

    // Not a class; callers report null as an error.
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

// Single shared ClassLoader; must be a real object, not null.
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

// java.lang.Class
bool Invoke_java_lang_Class(Interpreter* interp, const char* name, const DexValue* args,
                            size_t num_args, DexValue* result) {
    // Static; handle before reading args[0] as receiver.
    if (std::strcmp(name, "getPrimitiveClass") == 0) {
        const char* prim = num_args > 0 ? GetStringUtf8(args[0]) : nullptr;
        if (prim == nullptr || prim[0] == '\0') {
            interp->ThrowException("Ljava/lang/IllegalArgumentException;",
                                   "getPrimitiveClass with no name");
            return true;
        }
        // Translate source name to DEX descriptor.
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
            // Name the bad argument; only invented names reach here.
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
        DexClass* klass = interp->linker()->FindClassNoStub(desc.c_str());
        // A stub is a placeholder for a boot-classpath class KuDroid does not ship
        // yet; it has no methods or fields, so reporting success here would hand
        // the caller an unusable Class and surface the real problem much later as
        // a ClassCastException or a zero-valued field.
        if (klass == nullptr || klass->is_stub) {
            interp->ThrowException("Ljava/lang/ClassNotFoundException;", class_name);
            return true;
        }
        const bool initialize = num_args < 3 || args[1].i != 0;
        if (initialize && !interp->EnsureInitialized(klass)) return true;
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
        // One shared loader; null would break obfuscated loadClass calls.
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
            // Proxies report interface methods with the interface as owner.
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
        if (!interp->EnsureInitialized(klass)) return true;
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

// java.lang.reflect.{Method,Constructor,Field,Array}

const char* ReturnDescriptor(const DexMethod* m) {
    if (m == nullptr || m->signature == nullptr) return "V";
    const char* close = std::strchr(m->signature, ')');
    return close != nullptr ? close + 1 : "V";
}

