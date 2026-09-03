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
#include "kudroid/kuart/JitCompiler.h"
#include "kudroid/kuart/OatFile.h"

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

    // ── JIT ──
    //
    // Compilation is driven by a per-method call counter and is entirely optional: with
    // no executable memory (stock iOS without a debugger, LiveContainer JITLess) nothing
    // is ever compiled and every method runs interpreted, which is the correct answer
    // rather than a degraded one.

    // Reuse the previous run's profile, and record this run's into it.
    //
    // The gain is on the cold path: without a profile a hot loop must be interpreted
    // through kJitThreshold calls before it is compiled, and that cost is paid again on
    // every launch. Passing null detaches the profile.
    void set_oat_profile(OatFile* profile) { oat_profile_ = profile; }
    OatFile* oat_profile() const { return oat_profile_; }

    // Methods compiled, and calls that entered compiled code, this process.
    static uint64_t jit_compiled_methods();
    static uint64_t jit_entries();

    // Calls before a method is considered worth compiling.
    //
    // Low enough that a loop reaches it during startup, high enough that the thousands
    // of methods called once or twice — class initialisers, getters, framework glue —
    // are never compiled. Compiling those costs more than interpreting them.
    static constexpr uint32_t kJitThreshold = 64;

    // Throw exception by class descriptor.
    void ThrowException(const char* descriptor, const std::string& message);

    // The class whose bytecode is executing, for a native method that has to answer a
    // question about its caller.
    //
    // MethodHandles.lookup() is defined to report the calling class, and libraries act on
    // it: they compare lookupClass() against their own to decide whether the Lookup they
    // hold has the access they need. Returning a placeholder sends them down a fallback
    // path built for a different runtime.
    //
    // A libcore native method is dispatched from InvokeMethod without a frame of its own
    // (LibCoreInvoke is called in place of pushing one), so the top of the Java call stack
    // is already the caller. Null when there is no Java frame — a native downcall from JNI,
    // where there is no calling class to report.
    DexClass* CallerClass() const;

    // Print the pending exception as uncaught, with its stack trace. No-op when nothing
    // is pending.
    //
    // Called automatically by Execute() when an exception reaches depth 0 — no Java
    // frame caught it and it is being handed back to native code. That is the one point
    // where "uncaught" is a fact rather than a guess.
    //
    // The trace deliberately does NOT print at the throw site. Most exceptions a real
    // app throws are caught and expected: ULTRAKILL looks up an optional class by name
    // during startup and handles the ClassNotFoundException. Printing a five-frame trace
    // for each one reads as a fatal error and cost a round of investigation aimed at the
    // wrong problem.
    void ReportUncaughtException();

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

    // "com.google.androidgamesdk.GameActivity.initializeNativeCode(...)" for a method
    // reference that would not resolve.
    //
    // The message used to be "failed to resolve method index 45364", which is a number
    // only meaningful inside one DEX file — it named neither the class nor the method,
    // so a NoSuchMethodError could not be acted on without disassembling the APK. The
    // reference itself carries all three, so there is no reason to print the index.
    std::string DescribeMethodRef(const DexMethod* context, uint32_t method_idx) const;

    // Record an unresolvable method in classes.log, once per distinct reference.
    //
    // Separate from the auto-stub path: a boot-classpath method gets stubbed and
    // logged as MISSING_FRAMEWORK_METHOD, whereas this covers references that cannot
    // be stubbed at all — an app class whose method is absent, or a method on a class
    // that failed to load. Those are the ones that reach the caller as an exception,
    // and they were reported nowhere.
    void ReportUnresolvableMethod(const DexMethod* context, uint32_t method_idx);

    bool InvokeMethod(DexFrame* frame, const art::Instruction* inst, bool is_range,
                      art::Instruction::Code opcode);

    // Forward a call on a Proxy instance to its InvocationHandler.
    //
    // A synthesised proxy class declares no method bodies (see
    // DexClassLinker::GetOrCreateProxyClass), so any interface method invoked on it
    // resolves to something with no code. Rather than the AbstractMethodError that
    // would be correct for a normal class, the call becomes
    // handler.invoke(proxy, method, args) with the arguments boxed into an Object[],
    // and the boxed result unboxed back to the declared return type.
    //
    // Returns true when the call was handled — including when the handler threw, in
    // which case the pending exception is left set for the caller to propagate.
    bool InvokeProxyMethod(DexObject* proxy, DexMethod* method, const DexValue* args,
                           size_t num_args, DexValue* out);

    // filled-new-array{,/range}: allocate an array and fill it from the argument
    // registers. Result goes to frame->result() (read back by move-result-object)
    // exactly like an invoke, NOT into a destination register. Returns false when
    // an exception was thrown.
    bool FilledNewArray(DexFrame* frame, const art::Instruction* inst, bool is_range);

    bool FindCatchHandler(const art::CodeItemDataAccessor& accessor,
                           const DexMethod* method, uint32_t dex_pc,
                           uint32_t* handler_pc);

    // Run `method` as compiled code if it has any, compiling it when it becomes hot.
    //
    // Returns true when compiled code ran to a return, in which case *out holds the
    // result. Returns false when the method is not compiled, or when compiled code bailed
    // out at an unsupported instruction — in both cases the caller interprets, starting
    // from *resume_pc (0 when nothing ran).
    bool TryJit(DexFrame* frame, DexValue* out, uint32_t* resume_pc);

    static constexpr size_t kMaxCallDepth = 512;

    DexClassLinker* linker_ = nullptr;
    DexJniEnv* jni_env_ = nullptr;
    std::string last_error_;
    uint64_t instruction_limit_ = UINT64_MAX;
    OatFile* oat_profile_ = nullptr;

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
