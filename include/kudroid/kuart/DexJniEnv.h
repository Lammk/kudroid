// JNIEnv cho KuART: cầu nối giữa bytecode DEX và mã native ARM64 của game.
//
// Thay cho ~230 hàm vtable mà Avian/ART cung cấp. Đây là phần lớn nhất của
// KuART: game gọi JNI liên tục (FindClass, GetMethodID, CallVoidMethod,
// RegisterNatives...) trong khi bytecode chỉ chạy lúc onCreate + touch event.
//
// Ánh xạ handle JNI sang con trỏ KuART:
//   jclass    -> DexClass*   (qua bảng local/global ref)
//   jobject   -> DexObject*
//   jmethodID -> DexMethod*  (con trỏ trực tiếp, không qua bảng — ID phải bền)
//   jfieldID  -> DexField*
//
// Local ref dùng bảng theo frame để DeleteLocalRef và PopLocalFrame hoạt động
// đúng; global ref có bảng riêng không bị xoá theo frame.
#ifndef KUDROID_KUART_DEXJNIENV_H
#define KUDROID_KUART_DEXJNIENV_H

#include <jni.h>

#include <cstdarg>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "kudroid/kuart/DexClassLinker.h"
#include "kudroid/kuart/Interpreter.h"

namespace kudroid {
namespace kuart {

class DexJniEnv {
public:
    DexJniEnv(DexClassLinker* linker, Interpreter* interpreter);
    ~DexJniEnv();

    DexJniEnv(const DexJniEnv&) = delete;
    DexJniEnv& operator=(const DexJniEnv&) = delete;

    // Con trỏ truyền cho mã native. Layout khớp JNIEnv_ nên native cast được.
    JNIEnv* env() { return reinterpret_cast<JNIEnv*>(&env_storage_); }
    JavaVM* vm() { return reinterpret_cast<JavaVM*>(&vm_storage_); }

    static DexJniEnv* FromEnv(JNIEnv* env);
    static DexJniEnv* FromVm(JavaVM* vm);

    DexClassLinker* linker() { return linker_; }
    Interpreter* interpreter() { return interpreter_; }

    // ── quản lý reference ──
    jobject AddLocalRef(DexObject* obj);
    jobject AddGlobalRef(DexObject* obj);
    void DeleteLocalRef(jobject ref);
    void DeleteGlobalRef(jobject ref);
    static DexObject* Decode(jobject ref);

    void PushLocalFrame();
    void PopLocalFrame();

    // ── liên kết method native ──
    // RegisterNatives từ JNI_OnLoad của game.
    jint RegisterNatives(DexClass* klass, const JNINativeMethod* methods, jint count);

    // Tìm hàm native theo quy ước tên JNI (Java_pkg_Class_method) qua hook do
    // KuDroid cấp — trỏ tới LibraryManager của guest .so.
    using SymbolLookup = void* (*)(const char* symbol);
    void set_symbol_lookup(SymbolLookup fn) { symbol_lookup_ = fn; }

    // Liên kết method native chưa có native_fn. Trả false nếu không tìm thấy.
    bool LinkNativeMethod(DexMethod* method);

    // Gọi method native đã liên kết. args KHÔNG gồm JNIEnv/jclass — hàm này tự thêm.
    DexValue CallNative(DexMethod* method, const DexValue* args, size_t num_args);

    // ── gọi method Java từ native ──
    // `receiver` null cho method static. `virtual_dispatch` = chọn lại bản
    // override theo class thật của receiver (Call<Type>Method), tắt cho
    // CallNonvirtual<Type>Method và CallStatic<Type>Method.
    DexValue CallJavaA(DexObject* receiver, DexMethod* method, const jvalue* args,
                       bool virtual_dispatch);
    DexValue CallJavaV(DexObject* receiver, DexMethod* method, va_list args,
                       bool virtual_dispatch);

    DexObject* NewObjectA(DexClass* klass, DexMethod* ctor, const jvalue* args);

    static const char* MethodShorty(const DexMethod* method);

    // ── exception ──
    // Exception do bytecode ném nằm ở Interpreter, do native ném nằm ở đây;
    // ba hàm này hợp nhất hai nguồn để native chỉ thấy một trạng thái.
    void SetPendingException(DexObject* ex);
    DexObject* pending_exception() const;
    void ClearException();

    const std::string& last_error() const { return last_error_; }
    void set_last_error(const std::string& e) { last_error_ = e; }

    size_t NumLocalRefs() const;
    size_t NumGlobalRefs() const { return global_refs_.size(); }
    bool IsGlobalRef(DexObject* obj) const { return global_refs_.count(obj) != 0; }

private:
    void InitFunctionTable();

    // Bố cục khớp JNIEnv_ / JavaVM_ ở trường ĐẦU TIÊN (con trỏ bảng hàm) —
    // native chỉ đọc trường đó. Trường `self` phía sau để FromEnv/FromVm quay
    // ngược về DexJniEnv mà không cần bảng tra cứu toàn cục.
    struct EnvStorage {
        const JNINativeInterface_* functions = nullptr;
        DexJniEnv* self = nullptr;
    };
    struct VmStorage {
        const JNIInvokeInterface_* functions = nullptr;
        DexJniEnv* self = nullptr;
    };

    EnvStorage env_storage_{};
    VmStorage vm_storage_{};

    DexClassLinker* linker_ = nullptr;
    Interpreter* interpreter_ = nullptr;
    SymbolLookup symbol_lookup_ = nullptr;

    // Mỗi frame là một tập local ref. Frame ngoài cùng luôn tồn tại.
    std::vector<std::vector<DexObject*>> local_frames_;
    std::unordered_set<DexObject*> global_refs_;

    DexObject* pending_exception_ = nullptr;
    std::string last_error_;
};

}  // namespace kuart
}  // namespace kudroid

#endif  // KUDROID_KUART_DEXJNIENV_H
