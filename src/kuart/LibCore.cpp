#include "kudroid/kuart/LibCore.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <filesystem>
#include <random>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>

#include "kudroid/kudroid_bridge.h"
#include "kudroid/kuart/DexClass.h"
#include "kudroid/kuart/DexClassLinker.h"
#include "kudroid/kuart/DexHeap.h"
#include "kudroid/kuart/DexObject.h"
#include "kudroid/kuart/DexString.h"
#include "kudroid/kuart/Interpreter.h"
#include "kudroid/platform/JavaCanvasRenderer.h"

namespace kudroid {
namespace kuart {

namespace {

const char* GetStringUtf8(const DexValue& val) {
    if (val.l == nullptr) return "";
    auto* str = reinterpret_cast<DexString*>(val.l);
    return str->utf8 ? str->utf8 : "";
}

// ─────────────────────────────────────────────────────────────────────────────
// java.lang.Object
// ─────────────────────────────────────────────────────────────────────────────
bool Invoke_java_lang_Object(Interpreter* interp, const char* name, const DexValue* args,
                             size_t /*num_args*/, DexValue* result) {
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
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// java.lang.Class
// ─────────────────────────────────────────────────────────────────────────────
bool Invoke_java_lang_Class(Interpreter* interp, const char* name, const DexValue* args,
                            size_t num_args, DexValue* result) {
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
        if (klass == nullptr) {
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
    DexClass* klass = interp->linker()->ClassFromObject(self);
    if (klass == nullptr) klass = self->clazz;

    if (std::strcmp(name, "getName") == 0) {
        std::string pretty = klass ? klass->PrettyName() : "java.lang.Object";
        result->l = interp->linker()->NewString(pretty.c_str());
        return true;
    }
    if (std::strcmp(name, "getSuperclass") == 0) {
        if (klass && klass->superclass) {
            result->l = interp->linker()->GetClassObject(klass->superclass);
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
    if (std::strcmp(name, "getComponentType") == 0) {
        if (klass && klass->component_type) {
            result->l = interp->linker()->GetClassObject(klass->component_type);
        } else {
            result->l = nullptr;
        }
        return true;
    }
    if (std::strcmp(name, "isAssignableFrom") == 0) {
        DexObject* other_obj = num_args > 1 ? args[1].l : nullptr;
        DexClass* other_klass = other_obj ? interp->linker()->ClassFromObject(other_obj) : nullptr;
        if (!other_klass && other_obj) other_klass = other_obj->clazz;
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
        DexObject* new_obj = interp->linker()->AllocObject(klass);
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
// java.lang.System
// ─────────────────────────────────────────────────────────────────────────────
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
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%g", args[0].d);
        result->l = interp->linker()->NewString(buf);
        return true;
    }
    if (std::strcmp(name, "parseDouble") == 0) {
        const char* str = GetStringUtf8(args[0]);
        *result = DexValue::Double(std::strtod(str, nullptr));
        return true;
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// java.lang.String
// ─────────────────────────────────────────────────────────────────────────────
bool Invoke_java_lang_String(Interpreter* interp, const char* name, const DexValue* args,
                             size_t /*num_args*/, DexValue* result) {
    if (std::strcmp(name, "intern") == 0) {
        const char* utf8 = GetStringUtf8(args[0]);
        result->l = interp->linker()->InternString(utf8);
        return true;
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// java.lang.Thread
// ─────────────────────────────────────────────────────────────────────────────
bool Invoke_java_lang_Thread(Interpreter* interp, const char* name, const DexValue* args,
                             size_t /*num_args*/, DexValue* result) {
    if (std::strcmp(name, "currentThread") == 0) {
        static DexObject* s_main_thread = nullptr;
        if (!s_main_thread) {
            DexClass* thread_class = interp->linker()->FindClass("Ljava/lang/Thread;");
            s_main_thread = interp->linker()->AllocObject(thread_class);
        }
        result->l = s_main_thread;
        return true;
    }
    if (std::strcmp(name, "sleep") == 0) {
        int64_t ms = args[0].j;
        if (ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        return true;
    }
    if (std::strcmp(name, "yield") == 0) {
        std::this_thread::yield();
        return true;
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// java.io.File
// ─────────────────────────────────────────────────────────────────────────────
std::string GetFilePath(const DexValue& file_obj) {
    if (file_obj.l == nullptr) return "";
    DexClass* klass = file_obj.l->clazz;
    if (!klass) return "";
    DexField* f_path = klass->FindInstanceField("mPath", "Ljava/lang/String;");
    if (!f_path) f_path = klass->FindInstanceField("path", "Ljava/lang/String;");
    if (!f_path) return "";
    DexObject* path_str_obj = file_obj.l->GetField<DexObject*>(f_path->offset_or_slot);
    if (!path_str_obj) return "";
    auto* str = reinterpret_cast<DexString*>(path_str_obj);
    return str->utf8 ? str->utf8 : "";
}

bool Invoke_java_io_File(Interpreter* /*interp*/, const char* name, const DexValue* args,
                         size_t /*num_args*/, DexValue* result) {
    std::string path = GetFilePath(args[0]);
    if (path.empty()) {
        *result = DexValue::Int(0);
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
        const char* path = GetStringUtf8(args[0]);
        int fd = open(path, O_RDONLY);
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
        const char* path = GetStringUtf8(args[0]);
        bool append = args[1].i != 0;
        int flags = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);
        int fd = open(path, flags, 0644);
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
                                    size_t /*num_args*/, DexValue* /*result*/) {
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
    return false;
}

bool Invoke_android_app_Activity(Interpreter* /*interp*/, const char* name, const DexValue* args,
                                 size_t /*num_args*/, DexValue* /*result*/) {
    if (std::strcmp(name, "setRequestedOrientation_native") == 0) {
        kudroid_set_requested_orientation(args[0].i);
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

}  // namespace

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
    if (std::strcmp(desc, "Ljava/lang/String;") == 0) return Invoke_java_lang_String(interp, name, args, num_args, result);
    if (std::strcmp(desc, "Ljava/lang/Thread;") == 0) return Invoke_java_lang_Thread(interp, name, args, num_args, result);
    if (std::strcmp(desc, "Ljava/io/File;") == 0) return Invoke_java_io_File(interp, name, args, num_args, result);
    if (std::strcmp(desc, "Ljava/io/FileInputStream;") == 0) return Invoke_java_io_FileInputStream(interp, name, args, num_args, result);
    if (std::strcmp(desc, "Ljava/io/FileOutputStream;") == 0) return Invoke_java_io_FileOutputStream(interp, name, args, num_args, result);
    if (std::strcmp(desc, "Ljava/io/PrintStream;") == 0) return Invoke_java_io_PrintStream(interp, name, args, num_args, result);

    if (std::strcmp(desc, "Landroid/util/Log;") == 0) return Invoke_android_util_Log(interp, name, args, num_args, result);
    if (std::strcmp(desc, "Landroid/graphics/Canvas;") == 0) return Invoke_android_graphics_Canvas(interp, name, args, num_args, result);
    if (std::strcmp(desc, "Landroid/app/Activity;") == 0) return Invoke_android_app_Activity(interp, name, args, num_args, result);
    if (std::strcmp(desc, "Landroid/os/Vibrator;") == 0) return Invoke_android_os_Vibrator(interp, name, args, num_args, result);
    if (std::strcmp(desc, "Landroid/view/Window;") == 0 ||
        std::strcmp(desc, "Landroid/view/View;") == 0 ||
        std::strcmp(desc, "Landroid/os/PowerManager$WakeLock;") == 0) {
        return Invoke_keep_screen_on(interp, name, args, num_args, result);
    }

    return false;
}

bool LibCoreHasMethod(const DexMethod* method) {
    if (method == nullptr || method->declaring_class == nullptr) return false;
    const char* desc = method->declaring_class->descriptor;
    if (desc == nullptr) return false;

    return (std::strncmp(desc, "Ljava/", 6) == 0 ||
            std::strcmp(desc, "Landroid/util/Log;") == 0 ||
            std::strcmp(desc, "Landroid/graphics/Canvas;") == 0 ||
            std::strcmp(desc, "Landroid/app/Activity;") == 0 ||
            std::strcmp(desc, "Landroid/os/Vibrator;") == 0 ||
            std::strcmp(desc, "Landroid/view/Window;") == 0 ||
            std::strcmp(desc, "Landroid/view/View;") == 0 ||
            std::strcmp(desc, "Landroid/os/PowerManager$WakeLock;") == 0);
}

}  // namespace kuart
}  // namespace kudroid
