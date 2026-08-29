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
    void ClearPendingException() {
        pending_exception_ = nullptr;
        pending_exception_trace_.clear();
    }
    void SetPendingException(DexObject* ex) { pending_exception_ = ex; }

    // Stack trace captured when the in-flight exception was thrown. Empty when the
    // exception came from native code (JNI Throw) rather than from bytecode.
    const std::string& pending_exception_trace() const { return pending_exception_trace_; }

    const std::string& last_error() const { return last_error_; }
    uint64_t instructions_executed() const { return instructions_executed_; }
    void set_instruction_limit(uint64_t limit) { instruction_limit_ = limit; }

    // Run <clinit> if class is not yet initialized.
    bool EnsureInitialized(DexClass* klass);

    // Throw exception by class descriptor.
    void ThrowException(const char* descriptor, const std::string& message);

    // One entry of the per-thread Java call stack.
    //
    // The method pointer is recorded alongside the frame rather than read back out
    // of it later. A DexMethod is owned by its DexClass on the linker heap and lives
    // as long as the runtime, whereas a DexFrame lives on the C++ stack only for the
    // duration of one Execute() — so caching the method means a trace can be
    // rendered without dereferencing frame memory that may already be reclaimed.
    // The frame is still needed for dex_pc, which changes as the method runs.
    struct StackSlot {
        const DexMethod* method = nullptr;
        const DexFrame* frame = nullptr;
    };

    // Java-side call stack of the CURRENT thread, innermost frame first, one entry
    // per line: "    at com.foo.Bar.baz(Bar.java:12)".
    //
    // Without this, an exception thrown deep inside guest bytecode is reported as a
    // bare "ArrayIndexOutOfBoundsException: index 0 length 0" with no indication of
    // which method produced it — the failure that stopped Minecraft's
    // MainActivity.<clinit> could not be located from the log at all.
    std::string BuildStackTrace() const;

    // Description of the in-flight exception including its stack trace, for callers
    // that have to report an exception they are about to discard (JNI_OnLoad).
    std::string DescribePendingException() const;

    // Per-thread interpreter bookkeeping, for a caller that is about to leave the
    // interpreter by a route that does not unwind the C++ stack.
    //
    // Execute() maintains depth_ and call_stack_ with a RAII scope guard, which
    // covers a normal return and a C++ exception. It does NOT cover siglongjmp:
    // the JNI_OnLoad shield in kudroid_bridge.cpp jumps out of a faulting library
    // from a signal handler, and no destructor runs on the way. Every Execute()
    // frame entered underneath the shield therefore leaks its entry, leaving
    // call_stack_ holding pointers to DexFrame objects whose stack storage has been
    // reclaimed.
    //
    // The consequence was worse than a leak. The next exception thrown on that
    // thread called BuildStackTrace(), which walked the dead entries and dereferenced
    // a garbage DexMethod — turning a diagnosable Java exception into SIGSEGV inside
    // the very code meant to explain it. Minecraft's MainActivity.onCreate died this
    // way with its real exception never printed.
    struct ThreadState {
        size_t depth = 0;
        size_t frames = 0;
        // Recursion count of this thread inside the global VM lock. Execute() takes
        // it at depth 0 through a RAII guard, which siglongjmp skips just like the
        // others; leaving it held would deadlock every other Java thread.
        int vm_lock_depth = 0;
    };
    static ThreadState CaptureThreadState();

    // Drop anything the interpreter accumulated past `state`. Safe to call when
    // nothing leaked: it only ever shrinks.
    static void RestoreThreadState(const ThreadState& state);

private:
    DexValue ExecuteFrame(DexFrame* frame);
    DexValue RunBytecode(DexFrame* frame, const art::CodeItemDataAccessor& accessor);

    DexClass* ResolveClass(const DexMethod* context, uint32_t type_idx);
    DexField* ResolveField(const DexMethod* context, uint32_t field_idx, bool is_static);
    DexMethod* ResolveMethod(const DexMethod* context, uint32_t method_idx);

    // Name an unimplemented field, once per distinct field, in classes.log and on
    // stderr. Unlike a missing method a field cannot be stubbed — object layout is
    // fixed by LinkClass — so naming it is the only remedy.
    void ReportMissingField(const DexClass* klass, const char* name, const char* type,
                            bool is_static);

    // "iput android.view.inputmethod.EditorInfo.inputType : I" for an exception
    // message. The message used to be the bare opcode name, which identified
    // neither the class nor the field.
    std::string DescribeFieldRef(const DexMethod* context, uint32_t field_idx,
                                 const char* opcode) const;

    bool InvokeMethod(DexFrame* frame, const art::Instruction* inst, bool is_range,
                      art::Instruction::Code opcode);

    // filled-new-array{,/range}: allocate an array and fill it from the argument
    // registers. Result goes to frame->result() (read back by move-result-object)
    // exactly like an invoke, NOT into a destination register. Returns false when
    // an exception was thrown.
    bool FilledNewArray(DexFrame* frame, const art::Instruction* inst, bool is_range);

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

    // Live frames of this thread, outermost first.
    static thread_local std::vector<StackSlot> call_stack_;

    // Stack trace captured when the in-flight exception was thrown. Taken at throw
    // time because by the time a caller reports it the frames are already unwound.
    static thread_local std::string pending_exception_trace_;
};

}  // namespace kuart
}  // namespace kudroid

#endif  // KUDROID_KUART_INTERPRETER_H
