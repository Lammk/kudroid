// KuART Bytecode Interpreter.
// Replaces ART runtime interpreter (12.8k LOC heavily tied to mirror/ types).
// Implements DEX opcodes used by guest Java applications:
// move/const/arithmetic/compare/branch/field/array/invoke/try-catch.
#ifndef KUDROID_KUART_INTERPRETER_H
#define KUDROID_KUART_INTERPRETER_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "dex/code_item_accessors.h"
#include "dex/dex_instruction.h"

#include "kudroid/kuart/DexClass.h"
#include "kudroid/kuart/DexFrame.h"
#include "kudroid/kuart/DexValue.h"

namespace kudroid {
namespace kuart {

class DexClassLinker;
class DexJniEnv;

class Interpreter {
public:
    explicit Interpreter(DexClassLinker* linker) : linker_(linker) {}

    // JNIEnv used to invoke native methods.
    void set_jni_env(DexJniEnv* env) { jni_env_ = env; }
    DexJniEnv* jni_env() const { return jni_env_; }

    // Execute `method` with given `args` (long/double occupy ONE DexValue).
    DexValue Execute(const DexMethod* method, const DexValue* args, size_t num_args);

    DexClassLinker* linker() const { return linker_; }

    // Pending exception management.
    bool HasPendingException() const { return pending_exception_ != nullptr; }
    DexObject* pending_exception() const { return pending_exception_; }
    void ClearPendingException() { pending_exception_ = nullptr; }
    void SetPendingException(DexObject* ex) { pending_exception_ = ex; }

    const std::string& last_error() const { return last_error_; }
    uint64_t instructions_executed() const { return instructions_executed_; }
    void set_instruction_limit(uint64_t limit) { instruction_limit_ = limit; }

    // Run <clinit> if class is not yet initialized.
    bool EnsureInitialized(DexClass* klass);

    // Throw exception by class descriptor.
    void ThrowException(const char* descriptor, const std::string& message);

private:
    DexValue ExecuteFrame(DexFrame* frame);
    DexValue RunBytecode(DexFrame* frame, const art::CodeItemDataAccessor& accessor);

    DexClass* ResolveClass(const DexMethod* context, uint32_t type_idx);
    DexField* ResolveField(const DexMethod* context, uint32_t field_idx, bool is_static);
    DexMethod* ResolveMethod(const DexMethod* context, uint32_t method_idx);

    bool InvokeMethod(DexFrame* frame, const art::Instruction* inst, bool is_range,
                      art::Instruction::Code opcode);

    bool FindCatchHandler(const art::CodeItemDataAccessor& accessor,
                           const DexMethod* method, uint32_t dex_pc,
                           uint32_t* handler_pc);

    static constexpr size_t kMaxCallDepth = 512;

    DexClassLinker* linker_ = nullptr;
    DexJniEnv* jni_env_ = nullptr;
    std::string last_error_;
    uint64_t instruction_limit_ = 100'000'000;

    // Per-thread interpreter state. One Interpreter instance is shared by every
    // Java thread, so call depth, the instruction budget and the in-flight
    // exception must not be: a thread blocked in Object.wait() releases the VM
    // lock while its frames are still on the C++ stack.
    static thread_local DexObject* pending_exception_;
    static thread_local size_t depth_;
    static thread_local uint64_t instructions_executed_;
};

}  // namespace kuart
}  // namespace kudroid

#endif  // KUDROID_KUART_INTERPRETER_H
