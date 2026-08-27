// Interpreter DEX switch-based.
//
// Thay cho runtime/interpreter của ART (12.8k LOC, 41 chỗ include mirror/ nên
// không port được). Ở đây chỉ hiện thực nhóm opcode mà code Java của app Android
// thực sự chạy qua: move/const/arithmetic/compare/branch/field/array/invoke.
//
// KHÔNG có: verifier (DEX coi là trusted), JIT (interpreter thuần đủ vì game
// loop nằm trong .so native), OSR/deopt, profiling.
#ifndef KUDROID_KUART_INTERPRETER_H
#define KUDROID_KUART_INTERPRETER_H

#include <string>

#include "dex/dex_instruction.h"

#include "kudroid/kuart/DexClassLinker.h"
#include "kudroid/kuart/DexFrame.h"
#include "kudroid/kuart/DexValue.h"

namespace art {
class CodeItemDataAccessor;
}  // namespace art

namespace kudroid {
namespace kuart {

class DexJniEnv;

class Interpreter {
public:
    explicit Interpreter(DexClassLinker* linker) : linker_(linker) {}

    // JNIEnv dùng để gọi method native. Không có thì invoke native sẽ ném
    // UnsatisfiedLinkError.
    void set_jni_env(DexJniEnv* env) { jni_env_ = env; }
    DexJniEnv* jni_env() const { return jni_env_; }

    // Chạy `method` với tham số `args` (long/double = MỘT phần tử).
    // Trả về giá trị trả về; kiểu void thì bỏ qua.
    DexValue Execute(const DexMethod* method, const DexValue* args, size_t num_args);

    DexClassLinker* linker() const { return linker_; }

    // Có exception đang chờ xử lý hay không. Exception được bắt tại chỗ nếu
    // method có try/catch phủ dex_pc, ngược lại truyền lên caller.
    bool HasPendingException() const { return pending_exception_ != nullptr; }
    DexObject* pending_exception() const { return pending_exception_; }
    void ClearPendingException() { pending_exception_ = nullptr; }
    void SetPendingException(DexObject* ex) { pending_exception_ = ex; }

    const std::string& last_error() const { return last_error_; }

    // Đếm số instruction đã chạy — để test phát hiện vòng lặp vô hạn.
    uint64_t instructions_executed() const { return instructions_executed_; }

    // Chặn method chạy quá lâu (bytecode lỗi hoặc vòng lặp vô hạn).
    void set_instruction_limit(uint64_t limit) { instruction_limit_ = limit; }

    // Chạy <clinit> nếu class chưa khởi tạo. Public vì JNI (FindClass,
    // GetStaticFieldID, NewObject) cũng phải bảo đảm class đã khởi tạo.
    bool EnsureInitialized(DexClass* klass);

    // Ném exception theo tên class; dùng cho lỗi runtime của chính interpreter
    // và cho hiện thực libcore.
    void ThrowException(const char* descriptor, const std::string& message);

private:
    // Chạy thân method đã có frame nạp sẵn tham số; bắt exception qua try/catch.
    DexValue ExecuteFrame(DexFrame* frame);

    // Chạy bytecode từ frame->dex_pc() cho tới khi return hoặc có exception.
    // Trước mỗi instruction ghi lại pc vào frame để ExecuteFrame biết chỗ ném.
    DexValue RunBytecode(DexFrame* frame, const art::CodeItemDataAccessor& accessor);

    // Resolve theo index trong DEX của method đang chạy. Kết quả cache theo
    // (dex_file, index) vì bytecode tham chiếu cùng một index rất nhiều lần.
    DexClass* ResolveClass(const DexMethod* context, uint32_t type_idx);
    DexField* ResolveField(const DexMethod* context, uint32_t field_idx, bool is_static);
    DexMethod* ResolveMethod(const DexMethod* context, uint32_t method_idx);

    // Gọi method: chọn bản override qua vtable nếu là invoke-virtual.
    bool InvokeMethod(DexFrame* frame, const art::Instruction* inst, bool is_range,
                      art::Instruction::Code opcode);

    // Tìm handler try/catch trong method hiện tại phủ `dex_pc` và bắt được
    // exception đang chờ. Trả true và ghi địa chỉ handler vào `handler_pc`.
    bool FindCatchHandler(const art::CodeItemDataAccessor& accessor, const DexMethod* method,
                          uint32_t dex_pc, uint32_t* handler_pc);

    DexClassLinker* linker_ = nullptr;
    DexJniEnv* jni_env_ = nullptr;
    DexObject* pending_exception_ = nullptr;
    std::string last_error_;
    uint64_t instructions_executed_ = 0;
    uint64_t instruction_limit_ = 100'000'000;
    uint32_t depth_ = 0;

    static constexpr uint32_t kMaxCallDepth = 256;
};

}  // namespace kuart
}  // namespace kudroid

#endif  // KUDROID_KUART_INTERPRETER_H
