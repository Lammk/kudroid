#include "kudroid/kuart/Interpreter.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <pthread.h>
#include <set>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "dex/code_item_accessors-inl.h"
#include "dex/dex_file_exception_helpers.h"
#include "dex/dex_instruction.h"
#include "dex/dex_instruction-inl.h"

#include "kudroid/kuart/DexClassLinker.h"
#include "kudroid/kuart/DexJniEnv.h"
#include "kudroid/kuart/LibCore.h"
#include "kudroid/kuart/VmLock.h"
#include "kudroid/kuart/MethodResolutionTrace.h"
#include "kudroid/NativeCallTelemetry.h"

namespace kudroid {
namespace kuart {

namespace {

using art::Instruction;

// Integer division in Java: INT_MIN / -1 overflows (result not representable)
// but Java defines return INT_MIN itself, while C++ defines UB. Must block separately.
// The result is written through unsigned arithmetic: -a is itself UB when a is the
// minimum, and for b == -1 only a == INT_MIN reaches here with that value.
int32_t JavaIntDiv(int32_t a, int32_t b) {
    if (b == -1) return static_cast<int32_t>(0u - static_cast<uint32_t>(a));
    return a / b;
}
int32_t JavaIntRem(int32_t a, int32_t b) {
    if (b == -1) return 0;
    return a % b;
}
int64_t JavaLongDiv(int64_t a, int64_t b) {
    if (b == -1) return static_cast<int64_t>(0ull - static_cast<uint64_t>(a));
    return a / b;
}
int64_t JavaLongRem(int64_t a, int64_t b) {
    if (b == -1) return 0;
    return a % b;
}

// Bit shifting in Java only uses the 5 low bits (int) or the low 6 bits (long) of
// operand; C++ lets the translation amount >= width be UB.
int32_t JavaShl(int32_t v, int32_t s) { return static_cast<int32_t>(static_cast<uint32_t>(v) << (s & 31)); }
int32_t JavaShr(int32_t v, int32_t s) { return v >> (s & 31); }
int32_t JavaUshr(int32_t v, int32_t s) { return static_cast<int32_t>(static_cast<uint32_t>(v) >> (s & 31)); }
int64_t JavaShlLong(int64_t v, int32_t s) { return static_cast<int64_t>(static_cast<uint64_t>(v) << (s & 63)); }
int64_t JavaShrLong(int64_t v, int32_t s) { return v >> (s & 63); }
int64_t JavaUshrLong(int64_t v, int32_t s) { return static_cast<int64_t>(static_cast<uint64_t>(v) >> (s & 63)); }

// float/double -> int/long: Java forces saturate to MIN/MAX and NaN -> 0, but cast
// of C++ is UB when the value is out of range.
template <typename Int, typename Float>
Int JavaFloatToInt(Float f) {
    if (std::isnan(f)) return 0;
    constexpr Int kMin = std::numeric_limits<Int>::min();
    constexpr Int kMax = std::numeric_limits<Int>::max();
    if (f <= static_cast<Float>(kMin)) return kMin;
    if (f >= static_cast<Float>(kMax)) return kMax;
    return static_cast<Int>(f);
}

// cmpl/cmpg: different results when NaN is present (-1 for cmpl, 1 for cmpg).
template <typename T>
int32_t CompareFloat(T a, T b, int32_t nan_result) {
    if (std::isnan(a) || std::isnan(b)) return nan_result;
    if (a < b) return -1;
    if (a > b) return 1;
    return 0;
}

template <typename T>
int32_t CompareLong(T a, T b) {
    if (a < b) return -1;
    if (a > b) return 1;
    return 0;
}

}  // namespace

// Multiple ways keep colliding call sites and receiver types cached.
constexpr size_t kFastVirtualSets = 256;
constexpr size_t kFastVirtualWays = 4;
struct FastVirtualSlot {
    const void* site = nullptr;
    DexClass* receiver = nullptr;
    DexMethod* target = nullptr;
};
struct FastVirtualSet {
    FastVirtualSlot ways[kFastVirtualWays];
    unsigned next = 0;
};
std::atomic<uint64_t> g_fastVirtualGeneration{1};
thread_local uint64_t t_fastVirtualGeneration = 0;
thread_local FastVirtualSet t_fastVirtualCache[kFastVirtualSets];

constexpr size_t kFastFieldSets = 256;
constexpr size_t kFastFieldWays = 4;
struct FastFieldSlot {
    const DexClassLinker* linker = nullptr;
    const art::DexFile* dex = nullptr;
    const DexClass* owner = nullptr;
    uint32_t index = 0;
    bool is_static = false;
    DexField* field = nullptr;
};
struct FastFieldSet {
    FastFieldSlot ways[kFastFieldWays];
    unsigned next = 0;
};
thread_local uint64_t t_fastFieldGeneration = 0;
thread_local FastFieldSet t_fastFieldCache[kFastFieldSets];

constexpr size_t kFastTypeSlots = 512;
struct FastTypeSlot {
    const DexClassLinker* linker = nullptr;
    const art::DexFile* dex = nullptr;
    uint32_t index = 0;
    DexClass* klass = nullptr;
};
thread_local uint64_t t_fastTypeGeneration = 0;
thread_local FastTypeSlot t_fastTypeCache[kFastTypeSlots];

constexpr size_t kFastMethodSlots = 1024;
struct FastMethodSlot {
    const DexClassLinker* linker = nullptr;
    const art::DexFile* dex = nullptr;
    const DexClass* owner = nullptr;
    uint32_t index = 0;
    bool is_static = false;
    bool exact_owner = false;
    DexMethod* method = nullptr;
};
thread_local uint64_t t_fastMethodGeneration = 0;
thread_local FastMethodSlot t_fastMethodCache[kFastMethodSlots];

void ClearFastResolutionCaches() { g_fastVirtualGeneration.fetch_add(1); }

static FastVirtualSet& FastVirtualBucket(const void* site, const DexClass* receiver) {
    uintptr_t key = (reinterpret_cast<uintptr_t>(site) >> 1)
            ^ (reinterpret_cast<uintptr_t>(receiver) >> 4);
    key ^= key >> 17;
    return t_fastVirtualCache[key % kFastVirtualSets];
}

DexMethod* FastVirtualLookup(const void* site, DexClass* receiver) {
    const uint64_t generation = g_fastVirtualGeneration.load(std::memory_order_relaxed);
    if (t_fastVirtualGeneration != generation) {
        std::fill_n(t_fastVirtualCache, kFastVirtualSets, FastVirtualSet{});
        t_fastVirtualGeneration = generation;
    }
    const FastVirtualSet& bucket = FastVirtualBucket(site, receiver);
    for (const FastVirtualSlot& entry : bucket.ways) {
        if (entry.site == site && entry.receiver == receiver) return entry.target;
    }
    return nullptr;
}

void FastVirtualStore(const void* site, DexClass* receiver, DexMethod* target) {
    if (target == nullptr) return;
    FastVirtualSet& bucket = FastVirtualBucket(site, receiver);
    bucket.ways[bucket.next] = FastVirtualSlot{site, receiver, target};
    bucket.next = (bucket.next + 1) % kFastVirtualWays;
}

// ResolveMethod's stub + missing-log caches. Namespace scope (not
// function-static) so ClearResolveCaches can drop them on shutdown: they are
// keyed by DexClass* of the CURRENT heap, and serving them to the next app is
// a use-after-free of its method table.
std::map<std::tuple<const DexClass*, std::string, std::string>, std::unique_ptr<DexMethod>>
    g_stubMethods;
std::mutex g_stubMethodMtx;
std::set<std::string> g_loggedMissingMethods;

void ClearResolveCaches() {
    std::lock_guard<std::mutex> lock(g_stubMethodMtx);
    g_stubMethods.clear();
    g_loggedMissingMethods.clear();
    Interpreter::ClearBacktraceCaches();
    ClearFastResolutionCaches();
}

thread_local DexObject* Interpreter::pending_exception_ = nullptr;
thread_local size_t Interpreter::depth_ = 0;
thread_local uint64_t Interpreter::instructions_executed_ = 0;
thread_local std::vector<Interpreter::StackSlot> Interpreter::call_stack_;
thread_local std::string Interpreter::pending_exception_trace_;

namespace {

// The heap has no GC, so every captured backtrace is permanent. Identical stacks share
// one array, depth is capped, and capture degrades then stops as the budget fills.
constexpr size_t kBacktraceMaxFrames = 64;
constexpr size_t kBacktraceReducedFrames = 8;
constexpr size_t kBacktraceSoftBytes = 8u << 20;
constexpr size_t kBacktraceHardBytes = 16u << 20;

struct BacktraceCache {
    std::mutex mtx;
    // FNV-1a of the frame words -> interned backtraces with that hash.
    std::unordered_map<uint64_t, std::vector<DexArray*>> interned;
    // Backtrace -> its StackTraceElement[].
    std::unordered_map<const DexArray*, DexArray*> elements;
    size_t bytes = 0;
    bool budget_logged = false;
};

BacktraceCache& Backtraces() {
    static BacktraceCache cache;
    return cache;
}

}  // namespace

void Interpreter::ClearBacktraceCaches() {
    BacktraceCache& cache = Backtraces();
    std::lock_guard<std::mutex> lock(cache.mtx);
    cache.interned.clear();
    cache.elements.clear();
    cache.bytes = 0;
    cache.budget_logged = false;
}

DexArray* Interpreter::CaptureBacktrace(bool skip_throwable) {
    if (linker_ == nullptr) return nullptr;
    size_t top = call_stack_.size();
    if (skip_throwable) {
        const DexClass* throwable = linker_->FindClass("Ljava/lang/Throwable;");
        while (throwable != nullptr && top > 0) {
            const DexMethod* m = call_stack_[top - 1].method;
            if (m == nullptr || m->declaring_class == nullptr ||
                !m->declaring_class->IsSubClassOf(throwable)) break;
            --top;
        }
    }

    BacktraceCache& cache = Backtraces();
    std::lock_guard<std::mutex> lock(cache.mtx);
    if (cache.bytes >= kBacktraceHardBytes) {
        if (!cache.budget_logged) {
            cache.budget_logged = true;
            std::fprintf(stderr, "[KuART][STACKTRACE] backtrace budget exhausted\n");
        }
        return nullptr;
    }
    const size_t cap = cache.bytes >= kBacktraceSoftBytes ? kBacktraceReducedFrames
                                                          : kBacktraceMaxFrames;
    int64_t words[2 * kBacktraceMaxFrames];
    size_t n = 0;
    for (; top > 0 && n < cap; --top, ++n) {
        const StackSlot& slot = call_stack_[top - 1];
        words[2 * n] = static_cast<int64_t>(reinterpret_cast<intptr_t>(slot.method));
        words[2 * n + 1] = slot.frame != nullptr ? slot.frame->dex_pc() : 0;
    }
    if (n == 0) return nullptr;

    uint64_t hash = 1469598103934665603ull;
    for (size_t i = 0; i < 2 * n; ++i) {
        hash = (hash ^ static_cast<uint64_t>(words[i])) * 1099511628211ull;
    }
    std::vector<DexArray*>& bucket = cache.interned[hash];
    for (DexArray* existing : bucket) {
        if (existing->length == static_cast<int32_t>(2 * n) &&
            std::memcmp(existing->Data(), words, 2 * n * sizeof(int64_t)) == 0) {
            return existing;
        }
    }
    DexClass* klass = linker_->FindClass("[J");
    DexArray* array = klass != nullptr ? linker_->AllocArray(klass, static_cast<int32_t>(2 * n))
                                       : nullptr;
    if (array == nullptr) return nullptr;
    std::memcpy(array->Data(), words, 2 * n * sizeof(int64_t));
    cache.bytes += sizeof(DexArray) + 2 * n * sizeof(int64_t);
    bucket.push_back(array);
    return array;
}

DexArray* Interpreter::MaterializeBacktrace(DexArray* backtrace) {
    if (backtrace == nullptr || linker_ == nullptr) return nullptr;
    BacktraceCache& cache = Backtraces();
    {
        std::lock_guard<std::mutex> lock(cache.mtx);
        const auto it = cache.elements.find(backtrace);
        if (it != cache.elements.end()) return it->second;
    }
    DexClass* element_class = linker_->FindClass("Ljava/lang/StackTraceElement;");
    if (element_class == nullptr) return nullptr;
    const DexField* f_class = element_class->FindInstanceField("declaringClass", "Ljava/lang/String;");
    const DexField* f_method = element_class->FindInstanceField("methodName", "Ljava/lang/String;");
    const DexField* f_file = element_class->FindInstanceField("fileName", "Ljava/lang/String;");
    const DexField* f_line = element_class->FindInstanceField("lineNumber", "I");
    if (f_class == nullptr || f_method == nullptr || f_file == nullptr || f_line == nullptr) {
        return nullptr;
    }

    std::vector<DexObject*> items;
    items.reserve(static_cast<size_t>(backtrace->length) / 2);
    for (int32_t i = 0; i + 1 < backtrace->length; i += 2) {
        const auto* method = reinterpret_cast<const DexMethod*>(
            static_cast<intptr_t>(backtrace->Get<int64_t>(i)));
        const uint32_t dex_pc = static_cast<uint32_t>(backtrace->Get<int64_t>(i + 1));
        DexObject* element = linker_->AllocObject(element_class);
        if (element == nullptr) return nullptr;
        const char* source = nullptr;
        int32_t line = -1;
        std::string class_name = "?";
        const char* method_name = "?";
        if (method != nullptr) {
            if (method->declaring_class != nullptr) class_name = method->declaring_class->PrettyName();
            if (method->name != nullptr) method_name = method->name;
            if (method->dex_file != nullptr && method->declaring_class != nullptr &&
                method->declaring_class->class_def != nullptr) {
                source = method->dex_file->GetSourceFile(*method->declaring_class->class_def);
            }
            uint32_t found = 0;
            if (method->dex_file != nullptr && method->code_item != nullptr) {
                art::CodeItemDebugInfoAccessor accessor(*method->dex_file, method->code_item,
                                                        method->dex_method_index);
                if (accessor.GetLineNumForPc(dex_pc, &found)) line = static_cast<int32_t>(found);
            }
        }
        element->SetField<DexObject*>(f_class->offset_or_slot, linker_->InternString(class_name.c_str()));
        element->SetField<DexObject*>(f_method->offset_or_slot, linker_->InternString(method_name));
        element->SetField<DexObject*>(f_file->offset_or_slot,
                                      source != nullptr ? linker_->InternString(source) : nullptr);
        element->SetField<int32_t>(f_line->offset_or_slot, line);
        items.push_back(element);
    }
    DexArray* array = LibCoreNewRefArray(linker_, "[Ljava/lang/StackTraceElement;", items);
    if (array == nullptr) return nullptr;
    std::lock_guard<std::mutex> lock(cache.mtx);
    cache.elements.emplace(backtrace, array);
    return array;
}

void Interpreter::InitNativeThrowable(DexObject* exception, const char* message) {
    if (exception == nullptr || exception->clazz == nullptr || linker_ == nullptr) return;
    if (message != nullptr && message[0] != '\0') {
        const DexField* f_msg = exception->clazz->FindInstanceField("message", "Ljava/lang/String;");
        if (f_msg != nullptr) {
            exception->SetField<DexObject*>(f_msg->offset_or_slot, linker_->NewString(message));
        }
    }
    const DexField* f_backtrace = exception->clazz->FindInstanceField("backtrace", "Ljava/lang/Object;");
    if (f_backtrace == nullptr) return;
    if (DexArray* backtrace = CaptureBacktrace(false)) {
        exception->SetField<DexObject*>(f_backtrace->offset_or_slot, backtrace);
    }
}

#include "interpreter/Resolution.inc"
DexValue Interpreter::Execute(const DexMethod* method, const DexValue* args, size_t num_args) {
    // Builtins and bodyless methods also access shared VM state.
    std::optional<VmLockGuard> vm_lock;
    if (VmLockDepth() == 0) vm_lock.emplace();
    // Report uncaught exceptions on exit when no Java frame caught them.
    struct UncaughtReporter {
        Interpreter* self;
        ~UncaughtReporter() {
            if (self->depth_ == 0) self->ReportUncaughtException();
        }
    } uncaught_reporter{this};

    DexValue result;
    if (method == nullptr) {
        ThrowException("Ljava/lang/NullPointerException;", "method null");
        return result;
    }
    if (depth_ >= kMaxCallDepth) {
        ThrowException("Ljava/lang/StackOverflowError;", "maximum call depth exceeded");
        return result;
    }

    // Native methods have no bytecode; handle before the code check.
    if (method->IsNative()) {
        auto* target = const_cast<DexMethod*>(method);
        if (LibCoreInvoke(this, target, args, num_args, &result)) {
            if (HasPendingException()) return DexValue();
            return result;
        }
        if (jni_env_ != nullptr && jni_env_->LinkNativeMethod(target)) {
            const DexValue native_result = jni_env_->CallNative(target, args, num_args);
            if (HasPendingException()) return DexValue();
            return native_result;
        }
        ThrowException("Ljava/lang/UnsatisfiedLinkError;",
                       std::string("unbound native method: ") +
                           (method->name != nullptr ? method->name : "?"));
        return result;
    }

    if (method->code_item == nullptr) {
        // Proxy calls go to the handler; check before stub handling.
        if (!method->IsStatic() && num_args > 0 && linker_ != nullptr) {
            DexObject* receiver = args[0].l;
            DexClass* receiver_class = linker_->ClassOfObject(receiver);
            if (receiver_class != nullptr && receiver_class->is_proxy) {
                DexValue proxy_result;
                if (InvokeProxyMethod(receiver, const_cast<DexMethod*>(method), args,
                                      num_args, &proxy_result)) {
                    return proxy_result;
                }
            }
        }

        // Stub methods have no body; report the missing class.
        if (method->declaring_class != nullptr && method->declaring_class->is_stub) {
            ThrowException("Ljava/lang/NoClassDefFoundError;",
                           method->declaring_class->PrettyName() + "." +
                               (method->name != nullptr ? method->name : "?") +
                               " (class not implemented in KuDroid framework)");
            return result;
        }
        if (method->dex_file == nullptr || (method->declaring_class != nullptr && method->declaring_class->dex_file == nullptr)) {
            if (method->signature != nullptr) {
                const char* ret = std::strrchr(method->signature, ')');
                if (ret != nullptr && *(ret + 1) == '[') {
                    DexClass* arrKlass = linker_->FindClass(ret + 1);
                    if (arrKlass != nullptr) {
                        result.l = linker_->AllocArray(arrKlass, 0);
                    }
                }
            }
            return result;
        }
        ThrowException("Ljava/lang/AbstractMethodError;",
                       std::string("method without body: ") + (method->name ? method->name : "?"));
        return result;
    }

    DexFrame frame(method);
    const char* shorty = nullptr;
    if (method->dex_file != nullptr) {
        shorty = method->dex_file->GetMethodShorty(
            method->dex_file->GetMethodId(method->dex_method_index));
    }
    frame.LoadArguments(args, num_args, shorty, method->IsStatic());

    // Instruction limit is per top-level call, not cumulative.
    if (depth_ == 0) instructions_executed_ = 0;

    // Publish frame and depth; unwind by resizing to survive longjmps.
    struct StackEntry {
        size_t saved_frames;
        size_t saved_depth;
        StackEntry(const DexMethod* m, const DexFrame* f)
            : saved_frames(call_stack_.size()), saved_depth(depth_) {
            call_stack_.push_back(StackSlot{m, f});
            ++depth_;
        }
        ~StackEntry() {
            if (call_stack_.size() > saved_frames) call_stack_.resize(saved_frames);
            depth_ = saved_depth;
        }
    } stack_entry(method, &frame);

    if (method->telemetry_kind == 0)
        method->telemetry_kind = kudroid::java_call_should_trace(method->name, 3) ? 2 : 1;
    const bool trace_java = depth_ <= 3 || method->telemetry_kind == 2;
    if (trace_java) {
        kudroid::java_call_enter(
            method->declaring_class != nullptr ? method->declaring_class->descriptor : "?",
            method->name, method->signature, depth_ - 1);
    }
    struct JavaTelemetryExit {
        bool active;
        explicit JavaTelemetryExit(bool value) : active(value) {}
        ~JavaTelemetryExit() { if (active) kudroid::java_call_exit(); }
    } java_telemetry_exit(trace_java);
    result = ExecuteFrame(&frame);
    return result;
}

bool Interpreter::FindCatchHandler(const art::CodeItemDataAccessor& accessor,
                                   const DexMethod* method, uint32_t dex_pc,
                                   uint32_t* handler_pc) {
    if (accessor.TriesSize() == 0 || pending_exception_ == nullptr) return false;

    DexClass* ex_class = pending_exception_->clazz;
    for (art::CatchHandlerIterator it(accessor, dex_pc); it.HasNext(); it.Next()) {
        const art::dex::TypeIndex type_idx = it.GetHandlerTypeIndex();
        // kDexNoIndex16 = catch-all (finally), catches everything.
        if (type_idx.index_ == art::DexFile::kDexNoIndex16) {
            *handler_pc = it.GetHandlerAddress();
            return true;
        }
        DexClass* catch_class = ResolveClass(method, type_idx.index_);
        // If the class handler cannot be loaded, then that handler is ignored and is not considered captured
        // Yes - otherwise the exception will be thrown in the wrong place.
        if (catch_class == nullptr) continue;
        if (ex_class != nullptr && ex_class->IsSubClassOf(catch_class)) {
            *handler_pc = it.GetHandlerAddress();
            return true;
        }
    }
    return false;
}

bool Interpreter::EnsureInitialized(DexClass* klass) {
    if (klass == nullptr) return false;
    if (klass->status == DexClass::Status::kInitialized) return true;
    if (klass->status == DexClass::Status::kError) {
        if (!HasPendingException()) {
            ThrowException("Ljava/lang/NoClassDefFoundError;", klass->PrettyName());
        }
        return false;
    }

    // A second <clinit> would replace static sentinels and split object identity
    // from later field reads. The flag is per class and does not wait, so a
    // concurrent caller never blocks while holding the bytecode lock.
    bool expected = false;
    if (!klass->initialization_started.compare_exchange_strong(expected, true)) return true;

    if (linker_ != nullptr && !linker_->LinkClass(klass)) {
        klass->initialization_started.store(false);
        return false;
    }

    // Set kInitialized BEFORE running <clinit>: <clinit> is often referenced
    // that class itself (sputs into its static field) and will recur infinitely.
    klass->status = DexClass::Status::kInitialized;

    // The parent must be initialized before the child (JVM rule).
    if (klass->superclass != nullptr && !EnsureInitialized(klass->superclass)) {
        klass->status = DexClass::Status::kError;
        return false;
    }

    // Class initializers are declared by each class and are never inherited.
    DexMethod* clinit = nullptr;
    for (DexMethod& method : klass->direct_methods) {
        if (method.name != nullptr && std::strcmp(method.name, "<clinit>") == 0 &&
                method.signature != nullptr && std::strcmp(method.signature, "()V") == 0) {
            clinit = &method;
            break;
        }
    }
    if (clinit == nullptr || clinit->code_item == nullptr) return true;

    Execute(clinit, nullptr, 0);
    if (HasPendingException()) {
        klass->status = DexClass::Status::kError;
        return false;
    }
    return true;
}

// JIT dispatch

namespace {
std::atomic<uint64_t> g_jit_compiled_methods{0};
std::atomic<uint64_t> g_jit_entries{0};
}  // namespace

uint64_t Interpreter::jit_compiled_methods() { return g_jit_compiled_methods.load(); }
uint64_t Interpreter::jit_entries() { return g_jit_entries.load(); }

bool Interpreter::TryJit(DexFrame* frame, DexValue* out, uint32_t* resume_pc) {
    *resume_pc = 0;
    auto* method = const_cast<DexMethod*>(frame->method());
    if (method == nullptr) return false;

    // Nothing to do when the platform will not let us execute what we write. Checked
    // first and cached inside JitCache, so this costs one load on the common path.
    if (!JitCompiler::IsAvailable()) return false;

    if (method->jit_state == DexMethod::JitState::kRefused) return false;

    if (method->jit_state == DexMethod::JitState::kNotCompiled) {
        // A profile from the previous run turns a hot method into a first-call compile.
        // Without one the threshold decides; it is zero, so a supported method is
        // compiled the first time it runs instead of paying an interpreted warm-up.
        bool compileNow = false;
        const uint32_t checksum =
            method->dex_file != nullptr ? method->dex_file->GetLocationChecksum() : 0;
        if (oat_profile_ != nullptr && checksum != 0) {
            switch (oat_profile_->Lookup(checksum, method->dex_method_index)) {
                case OatFile::MethodState::kHot:
                    compileNow = true;
                    break;
                case OatFile::MethodState::kRefused:
                    // The compiler declined this bytecode last run and the bytecode has
                    // not changed (the checksum matched), so it will decline again.
                    method->jit_state = DexMethod::JitState::kRefused;
                    return false;
                case OatFile::MethodState::kUnknown:
                    break;
            }
        }

        if (!compileNow) {
            // Saturate rather than wrap: a method called four billion times must not
            // fall back below the threshold and be recompiled.
            if (method->call_count < kJitThreshold) {
                ++method->call_count;
                return false;
            }
            compileNow = true;
        }

        std::string reason;
        JitEntry entry = JitCompiler::Compile(method, &reason);
        if (entry == nullptr) {
            method->jit_state = DexMethod::JitState::kRefused;
            if (oat_profile_ != nullptr && checksum != 0) {
                oat_profile_->Note(checksum, method->dex_method_index,
                                   OatFile::MethodState::kRefused);
            }
            // Once per method, and only for methods hot enough to be worth compiling:
            // this names the opcode to implement next, which is the only actionable
            // output the JIT produces.
#ifdef KUDROID_DEBUG_JIT
            std::fprintf(stderr, "[KuART][JIT] not compiled: %s.%s (%s)\n",
                         method->declaring_class != nullptr
                             ? method->declaring_class->PrettyName().c_str()
                             : "?",
                         method->name != nullptr ? method->name : "?", reason.c_str());
#endif
            return false;
        }
        method->jit_code = reinterpret_cast<void*>(entry);
        method->jit_state = DexMethod::JitState::kCompiled;
        g_jit_compiled_methods.fetch_add(1);
        if (oat_profile_ != nullptr && checksum != 0) {
            oat_profile_->Note(checksum, method->dex_method_index,
                               OatFile::MethodState::kHot);
        }
        std::fprintf(stderr, "[KuART][JIT] compiled %s.%s\n",
                     method->declaring_class != nullptr
                         ? method->declaring_class->PrettyName().c_str()
                         : "?",
                     method->name != nullptr ? method->name : "?");
    }

    if (method->jit_code == nullptr) return false;

    // Compiled code reads and writes the frame's register array directly, so the frame
    // remains authoritative at every instruction boundary. That is what makes the bail
    // below correct: the interpreter can pick up at any dex_pc and see exactly the state
    // the compiled code left.
    auto entry = reinterpret_cast<JitEntry>(method->jit_code);
    DexValue result;
    uint32_t bail_pc = 0;
    g_jit_entries.fetch_add(1);
    const int32_t rc = entry(frame->registers(), &result, &bail_pc);
    if (rc >= 0) {
        *out = result;
        return true;
    }
    *resume_pc = bail_pc;
    return false;
}

DexValue Interpreter::ExecuteFrame(DexFrame* frame) {
    const DexMethod* method = frame->method();
    art::CodeItemDataAccessor accessor(*method->dex_file, method->code_item);

    // Compiled code first, if this method has any. A bail sets the dex_pc the
    // interpreter should continue from - compiled code and the interpreter share the
    // frame's register array, so resuming mid-method needs no state transfer.
    {
        DexValue jitResult;
        uint32_t resume_pc = 0;
        if (TryJit(frame, &jitResult, &resume_pc)) return jitResult;
        if (resume_pc != 0) frame->set_dex_pc(resume_pc);
    }

    for (;;) {
        const DexValue result = RunBytecode(frame, accessor);
        if (!HasPendingException()) return result;

        // Exception thrown at frame->dex_pc(); if this method has an overlay handler
        // That place is captured here, otherwise transmitted to the caller.
        uint32_t handler_pc = 0;
        if (!FindCatchHandler(accessor, method, frame->dex_pc(), &handler_pc)) return result;

        frame->set_caught_exception(pending_exception_);
        ClearPendingException();
        frame->set_dex_pc(handler_pc);
    }
}

// Report uncaught exceptions exiting to native code with their trace.
void Interpreter::ReportUncaughtException() {
    if (!HasPendingException()) return;
    std::fprintf(stderr, "[KuART][EXCEPTION] 💥 uncaught: %s\n", last_error_.c_str());
    // Also print Throwable.message, which last_error_ omits.
    if (pending_exception_ != nullptr && pending_exception_->clazz != nullptr) {
        if (DexField* f_msg = pending_exception_->clazz->FindInstanceField(
                "message", "Ljava/lang/String;")) {
            DexObject* msg_obj =
                pending_exception_->GetField<DexObject*>(f_msg->offset_or_slot);
            if (msg_obj != nullptr && msg_obj->clazz != nullptr &&
                msg_obj->clazz->descriptor != nullptr &&
                std::strcmp(msg_obj->clazz->descriptor, "Ljava/lang/String;") == 0) {
                const char* utf8 = reinterpret_cast<DexString*>(msg_obj)->utf8;
                if (utf8 != nullptr && utf8[0] != '\0') {
                    std::fprintf(stderr, "[KuART][EXCEPTION] message: %s\n", utf8);
                }
            }
        }
    }
    if (!pending_exception_trace_.empty()) {
        std::fputs(pending_exception_trace_.c_str(), stderr);
    }
}

#include "interpreter/Bytecode.inc"

}  // namespace kuart
}  // namespace kudroid
