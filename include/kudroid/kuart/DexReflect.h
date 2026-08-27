// Reflection tối thiểu cho KuART.
//
// Chỉ đủ những gì ActivityThread và code khởi động app dùng:
//   Class.forName(name) → newInstance() → getMethod()/invoke()
// KHÔNG có annotation, generic, Field.setAccessible, Proxy, MethodHandle.
//
// Không hiện thực bằng bytecode Java (không có libcore) mà bằng C++ trực tiếp:
// framework/*.java khai báo các method này là `native`, DexJniEnv liên kết chúng
// qua bảng đăng ký sẵn trong RegisterBuiltins().
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

    // Class.forName("com.foo.Bar") — nhận tên có DẤU CHẤM như Java, tự đổi sang
    // descriptor. Chạy <clinit> như Java thật (initialize = true).
    DexClass* ForName(const char* dotted_name);

    // Class.newInstance(): gọi constructor không tham số.
    DexObject* NewInstance(DexClass* klass);

    // Class.getName() dạng "com.foo.Bar" (mảng vẫn giữ descriptor như Java).
    std::string GetName(const DexClass* klass) const;

    // Class.getMethod/getDeclaredMethod: tìm theo tên + chữ ký DEX. Java thật
    // tìm theo mảng Class[] parameterTypes, nhưng bytecode gọi qua đây luôn biết
    // chữ ký nên nhận thẳng chữ ký cho gọn và chính xác hơn.
    DexMethod* FindMethod(DexClass* klass, const char* name, const char* signature);

    // Method.invoke(receiver, args). `receiver` null cho method static.
    DexValue Invoke(DexMethod* method, DexObject* receiver, const DexValue* args,
                    size_t num_args);

    // Đổi "com.foo.Bar" ⇄ "Lcom/foo/Bar;". Tên mảng ("[I", "[Lcom.foo.Bar;")
    // giữ nguyên phần '[' và chỉ đổi dấu chấm.
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
