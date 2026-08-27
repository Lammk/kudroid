#include "kudroid/kuart/Interpreter.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>

#include "dex/code_item_accessors-inl.h"
#include "dex/dex_file_exception_helpers.h"
#include "dex/dex_instruction.h"
#include "dex/dex_instruction-inl.h"

#include "kudroid/kuart/DexJniEnv.h"
#include "kudroid/kuart/LibCore.h"
#include "kudroid/kuart/VmLock.h"

namespace kudroid {
namespace kuart {

namespace {

using art::Instruction;

// Integer division in Java: INT_MIN / -1 overflows (result not representable)
// but Java defines return INT_MIN itself, while C++ defines UB. Must block separately.
int32_t JavaIntDiv(int32_t a, int32_t b) {
    if (b == -1) return -a;
    return a / b;
}
int32_t JavaIntRem(int32_t a, int32_t b) {
    if (b == -1) return 0;
    return a % b;
}
int64_t JavaLongDiv(int64_t a, int64_t b) {
    if (b == -1) return -a;
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

thread_local DexObject* Interpreter::pending_exception_ = nullptr;
thread_local size_t Interpreter::depth_ = 0;
thread_local uint64_t Interpreter::instructions_executed_ = 0;

void Interpreter::ThrowException(const char* descriptor, const std::string& message) {
    last_error_ = std::string(descriptor) + ": " + message;
    DexClass* klass = linker_ != nullptr ? linker_->FindClass(descriptor) : nullptr;
    if (klass != nullptr) {
        pending_exception_ = linker_->AllocObject(klass);
    }
    if (pending_exception_ == nullptr) {
        // No class exception in classpath (framework not loaded) —
        // You still have to stop the method, using an empty object as a flag.
        static DexObject placeholder;
        pending_exception_ = &placeholder;
    }
}

DexClass* Interpreter::ResolveClass(const DexMethod* context, uint32_t type_idx) {
    if (context == nullptr || context->dex_file == nullptr || linker_ == nullptr) return nullptr;
    const char* descriptor =
        context->dex_file->StringByTypeIdx(art::dex::TypeIndex(static_cast<uint16_t>(type_idx)));
    if (descriptor == nullptr) return nullptr;
    return linker_->FindClass(descriptor);
}

DexField* Interpreter::ResolveField(const DexMethod* context, uint32_t field_idx,
                                    bool is_static) {
    if (context == nullptr || context->dex_file == nullptr) return nullptr;
    const art::DexFile& dex_file = *context->dex_file;
    const art::dex::FieldId& field_id = dex_file.GetFieldId(field_idx);

    DexClass* klass = linker_->FindClass(dex_file.GetFieldDeclaringClassDescriptor(field_id));
    if (klass == nullptr) return nullptr;

    const char* name = dex_file.GetFieldName(field_id);
    const char* type = dex_file.GetFieldTypeDescriptor(field_id);
    return is_static ? klass->FindStaticField(name, type) : klass->FindInstanceField(name, type);
}

DexMethod* Interpreter::ResolveMethod(const DexMethod* context, uint32_t method_idx) {
    if (context == nullptr || context->dex_file == nullptr) return nullptr;
    const art::DexFile& dex_file = *context->dex_file;
    const art::dex::MethodId& method_id = dex_file.GetMethodId(method_idx);

    const char* class_descriptor =
        dex_file.StringByTypeIdx(art::dex::TypeIndex(method_id.class_idx_.index_));
    DexClass* klass = linker_->FindClass(class_descriptor);
    if (klass == nullptr) return nullptr;

    const char* name = dex_file.GetMethodName(method_id);
    const std::string signature = dex_file.GetMethodSignature(method_id).ToString();

    if (DexMethod* m = klass->FindDirectMethod(name, signature.c_str())) return m;
    if (DexMethod* m = klass->FindVirtualMethod(name, signature.c_str())) return m;

    if (klass->dex_file == nullptr) {
        static std::vector<std::unique_ptr<DexMethod>> s_stubMethods;
        static std::mutex s_stubMethodMtx;
        std::lock_guard<std::mutex> lock(s_stubMethodMtx);
        auto stubM = std::make_unique<DexMethod>();
        stubM->name = name;
        stubM->declaring_class = klass;
        stubM->access_flags = art::kAccPublic;
        stubM->code_item = nullptr;
        DexMethod* res = stubM.get();
        s_stubMethods.push_back(std::move(stubM));
        return res;
    }
    return nullptr;
}

bool Interpreter::InvokeMethod(DexFrame* frame, const art::Instruction* inst, bool is_range,
                              Instruction::Code opcode) {
    const uint32_t method_idx = is_range ? inst->VRegB_3rc() : inst->VRegB_35c();
    DexMethod* target = ResolveMethod(frame->method(), method_idx);
    if (target == nullptr) {
        ThrowException("Ljava/lang/NoSuchMethodError;",
                       "failed to resolve method index " + std::to_string(method_idx));
        return false;
    }

    // Collect register containing parameters. The /range form uses a continuous range, the normal form
    // Use a list of up to 5 discrete registers.
    uint32_t arg_regs[art::Instruction::kMaxVarArgRegs];
    uint32_t arg_count;
    uint32_t first_reg = 0;
    if (is_range) {
        arg_count = inst->VRegA_3rc();
        first_reg = inst->VRegC_3rc();
    } else {
        arg_count = inst->GetVarArgs(arg_regs);
    }

    const bool is_static = opcode == Instruction::INVOKE_STATIC ||
                           opcode == Instruction::INVOKE_STATIC_RANGE;

    // The static method is the first point the class is used if the app does not have a new object.
    if (is_static) {
        EnsureInitialized(target->declaring_class);
        if (HasPendingException()) return false;
    }

    // Register wide takes up 2 slots in the parameter list but is only one price
    // value — must ignore second slot when collecting.
    const char* shorty = nullptr;
    if (target->dex_file != nullptr) {
        shorty = target->dex_file->GetMethodShorty(
            target->dex_file->GetMethodId(target->dex_method_index));
    }

    std::vector<DexValue> args;
    args.reserve(arg_count);

    uint32_t slot = 0;
    const auto reg_at = [&](uint32_t i) {
        return is_range ? first_reg + i : arg_regs[i];
    };

    DexObject* receiver = nullptr;
    if (!is_static) {
        if (arg_count == 0) {
            ThrowException("Ljava/lang/VerifyError;", "invoke instance missing receiver");
            return false;
        }
        receiver = frame->GetRef(reg_at(0));
        if (receiver == nullptr) {
            ThrowException("Ljava/lang/NullPointerException;",
                           std::string("call ") + target->name + " on null");
            return false;
        }
        args.push_back(DexValue::Ref(receiver));
        slot = 1;
    }

    if (shorty != nullptr) {
        for (const char* p = shorty + 1; *p != '\0' && slot < arg_count; ++p) {
            args.push_back(frame->Get(reg_at(slot)));
            slot += (*p == 'J' || *p == 'D') ? 2 : 1;
        }
    }

    // invoke-virtual/interface: choose the override version according to the receiver's real class.
    if ((opcode == Instruction::INVOKE_VIRTUAL ||
         opcode == Instruction::INVOKE_VIRTUAL_RANGE ||
         opcode == Instruction::INVOKE_INTERFACE ||
         opcode == Instruction::INVOKE_INTERFACE_RANGE) &&
        receiver != nullptr && receiver->clazz != nullptr) {
        DexMethod* resolved = receiver->clazz->FindVirtualMethod(target->name, target->signature);
        if (resolved != nullptr) target = resolved;
    }

    if (target->IsNative()) {
        // The self-written libcore is called directly in C++, without going through the JNI ABI.
        DexValue lib_result;
        if (LibCoreInvoke(this, target, args.data(), args.size(), &lib_result)) {
            if (HasPendingException()) return false;
            frame->set_result(lib_result);
            return true;
        }
        if (jni_env_ == nullptr || !jni_env_->LinkNativeMethod(target)) {
            ThrowException("Ljava/lang/UnsatisfiedLinkError;",
                           std::string("unbound native method: ") + target->name);
            return false;
        }
        const DexValue native_result = jni_env_->CallNative(target, args.data(), args.size());
        if (HasPendingException()) return false;
        frame->set_result(native_result);
        return true;
    }

    const DexValue result = Execute(target, args.data(), args.size());
    if (HasPendingException()) return false;
    frame->set_result(result);
    return true;
}

DexValue Interpreter::Execute(const DexMethod* method, const DexValue* args, size_t num_args) {
    DexValue result;
    if (method == nullptr) {
        ThrowException("Ljava/lang/NullPointerException;", "method null");
        return result;
    }
    if (depth_ >= kMaxCallDepth) {
        ThrowException("Ljava/lang/StackOverflowError;", "maximum call depth exceeded");
        return result;
    }

    // Native methods have no bytecode. Reflection (Method.invoke) and JNI both
    // land here, so the native path must be handled before the code_item check.
    if (method->IsNative()) {
        auto* target = const_cast<DexMethod*>(method);
        if (LibCoreInvoke(this, target, args, num_args, &result)) return result;
        if (jni_env_ != nullptr && jni_env_->LinkNativeMethod(target)) {
            return jni_env_->CallNative(target, args, num_args);
        }
        ThrowException("Ljava/lang/UnsatisfiedLinkError;",
                       std::string("unbound native method: ") +
                           (method->name != nullptr ? method->name : "?"));
        return result;
    }

    if (method->code_item == nullptr) {
        if (method->declaring_class != nullptr && method->declaring_class->dex_file == nullptr) {
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

    // The instruction limit is calculated for each missed call, not cumulative
    // multiple calls — otherwise a method that runs for a long time will cause subsequent calls to fail.
    if (depth_ == 0) instructions_executed_ = 0;

    // Serialise bytecode across Java threads. Taken only at the outermost call so
    // nested invokes stay cheap; the mutex is recursive either way.
    std::unique_ptr<VmLockGuard> vm_lock;
    if (depth_ == 0) vm_lock = std::make_unique<VmLockGuard>();

    ++depth_;
    result = ExecuteFrame(&frame);
    --depth_;
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
        // Yes — otherwise the exception will be thrown in the wrong place.
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
    if (klass->status == DexClass::Status::kError) return false;
    if (linker_ != nullptr && !linker_->LinkClass(klass)) return false;

    // Set kInitialized BEFORE running <clinit>: <clinit> is often referenced
    // that class itself (sputs into its static field) and will recur infinitely.
    klass->status = DexClass::Status::kInitialized;

    // The parent must be initialized before the child (JVM rule).
    if (klass->superclass != nullptr) EnsureInitialized(klass->superclass);

    DexMethod* clinit = klass->FindDirectMethod("<clinit>", "()V");
    if (clinit == nullptr || clinit->code_item == nullptr) return true;

    Execute(clinit, nullptr, 0);
    if (HasPendingException()) {
        klass->status = DexClass::Status::kError;
        return false;
    }
    return true;
}

DexValue Interpreter::ExecuteFrame(DexFrame* frame) {
    const DexMethod* method = frame->method();
    art::CodeItemDataAccessor accessor(*method->dex_file, method->code_item);

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

DexValue Interpreter::RunBytecode(DexFrame* frame, const art::CodeItemDataAccessor& accessor) {
    DexValue return_value;
    const DexMethod* method = frame->method();

    const uint16_t* insns = accessor.Insns();
    uint32_t dex_pc = frame->dex_pc();
    const uint32_t insns_size = accessor.InsnsSizeInCodeUnits();

    while (dex_pc < insns_size) {
        if (++instructions_executed_ > instruction_limit_) {
            ThrowException("Ljava/lang/InternalError;", "instruction number limit exceeded");
            return return_value;
        }

        // Record the computer before running so that ExecuteFrame knows which instructions to throw.
        frame->set_dex_pc(dex_pc);

        const Instruction* inst = Instruction::At(insns + dex_pc);
        const uint32_t next_pc =
            dex_pc + static_cast<uint32_t>(inst->SizeInCodeUnits());

        switch (inst->Opcode()) {
            case Instruction::NOP:
                break;

            // ── move ──
            case Instruction::MOVE:
            case Instruction::MOVE_OBJECT:
                frame->Set(inst->VRegA_12x(), frame->Get(inst->VRegB_12x()));
                break;
            case Instruction::MOVE_FROM16:
            case Instruction::MOVE_OBJECT_FROM16:
                frame->Set(inst->VRegA_22x(), frame->Get(inst->VRegB_22x()));
                break;
            case Instruction::MOVE_16:
            case Instruction::MOVE_OBJECT_16:
                frame->Set(inst->VRegA_32x(), frame->Get(inst->VRegB_32x()));
                break;
            case Instruction::MOVE_WIDE:
                frame->Set(inst->VRegA_12x(), frame->Get(inst->VRegB_12x()));
                break;
            case Instruction::MOVE_WIDE_FROM16:
                frame->Set(inst->VRegA_22x(), frame->Get(inst->VRegB_22x()));
                break;
            case Instruction::MOVE_WIDE_16:
                frame->Set(inst->VRegA_32x(), frame->Get(inst->VRegB_32x()));
                break;

            case Instruction::MOVE_RESULT:
            case Instruction::MOVE_RESULT_WIDE:
            case Instruction::MOVE_RESULT_OBJECT:
                frame->Set(inst->VRegA_11x(), frame->result());
                break;

            // Only valid at the beginning of the catch handler; ExecuteFrame is already set.
            case Instruction::MOVE_EXCEPTION:
                frame->SetRef(inst->VRegA_11x(), frame->caught_exception());
                frame->set_caught_exception(nullptr);
                break;

            // ── const ──
            case Instruction::CONST_4:
                frame->SetInt(inst->VRegA_11n(), inst->VRegB_11n());
                break;
            case Instruction::CONST_16:
                frame->SetInt(inst->VRegA_21s(), inst->VRegB_21s());
                break;
            case Instruction::CONST:
                frame->SetInt(inst->VRegA_31i(), inst->VRegB_31i());
                break;
            case Instruction::CONST_HIGH16:
                frame->SetInt(inst->VRegA_21h(),
                              static_cast<int32_t>(inst->VRegB_21h()) << 16);
                break;
            case Instruction::CONST_WIDE_16:
                frame->SetLong(inst->VRegA_21s(), inst->VRegB_21s());
                break;
            case Instruction::CONST_WIDE_32:
                frame->SetLong(inst->VRegA_31i(), inst->VRegB_31i());
                break;
            case Instruction::CONST_WIDE:
                frame->SetLong(inst->VRegA_51l(),
                               static_cast<int64_t>(inst->WideVRegB()));
                break;
            case Instruction::CONST_WIDE_HIGH16:
                frame->SetLong(inst->VRegA_21h(),
                               static_cast<int64_t>(inst->VRegB_21h()) << 48);
                break;

            // ── return ──
            case Instruction::RETURN_VOID:
            case Instruction::RETURN_VOID_NO_BARRIER:
                return return_value;
            case Instruction::RETURN:
            case Instruction::RETURN_WIDE:
            case Instruction::RETURN_OBJECT:
                return frame->Get(inst->VRegA_11x());

            // ── compare ──
            case Instruction::CMPL_FLOAT:
                frame->SetInt(inst->VRegA_23x(),
                              CompareFloat(frame->GetFloat(inst->VRegB_23x()),
                                           frame->GetFloat(inst->VRegC_23x()), -1));
                break;
            case Instruction::CMPG_FLOAT:
                frame->SetInt(inst->VRegA_23x(),
                              CompareFloat(frame->GetFloat(inst->VRegB_23x()),
                                           frame->GetFloat(inst->VRegC_23x()), 1));
                break;
            case Instruction::CMPL_DOUBLE:
                frame->SetInt(inst->VRegA_23x(),
                              CompareFloat(frame->GetDouble(inst->VRegB_23x()),
                                           frame->GetDouble(inst->VRegC_23x()), -1));
                break;
            case Instruction::CMPG_DOUBLE:
                frame->SetInt(inst->VRegA_23x(),
                              CompareFloat(frame->GetDouble(inst->VRegB_23x()),
                                           frame->GetDouble(inst->VRegC_23x()), 1));
                break;
            case Instruction::CMP_LONG:
                frame->SetInt(inst->VRegA_23x(),
                              CompareLong(frame->GetLong(inst->VRegB_23x()),
                                          frame->GetLong(inst->VRegC_23x())));
                break;

            // ── jump ──
            case Instruction::GOTO:
                dex_pc += inst->VRegA_10t();
                continue;
            case Instruction::GOTO_16:
                dex_pc += inst->VRegA_20t();
                continue;
            case Instruction::GOTO_32:
                dex_pc += inst->VRegA_30t();
                continue;

            case Instruction::IF_EQ: case Instruction::IF_NE:
            case Instruction::IF_LT: case Instruction::IF_GE:
            case Instruction::IF_GT: case Instruction::IF_LE: {
                const int32_t a = frame->GetInt(inst->VRegA_22t());
                const int32_t b = frame->GetInt(inst->VRegB_22t());
                bool taken = false;
                switch (inst->Opcode()) {
                    case Instruction::IF_EQ: taken = a == b; break;
                    case Instruction::IF_NE: taken = a != b; break;
                    case Instruction::IF_LT: taken = a < b; break;
                    case Instruction::IF_GE: taken = a >= b; break;
                    case Instruction::IF_GT: taken = a > b; break;
                    default:                 taken = a <= b; break;
                }
                if (taken) {
                    dex_pc += inst->VRegC_22t();
                    continue;
                }
                break;
            }

            case Instruction::IF_EQZ: case Instruction::IF_NEZ:
            case Instruction::IF_LTZ: case Instruction::IF_GEZ:
            case Instruction::IF_GTZ: case Instruction::IF_LEZ: {
                const int32_t a = frame->GetInt(inst->VRegA_21t());
                bool taken = false;
                switch (inst->Opcode()) {
                    case Instruction::IF_EQZ: taken = a == 0; break;
                    case Instruction::IF_NEZ: taken = a != 0; break;
                    case Instruction::IF_LTZ: taken = a < 0; break;
                    case Instruction::IF_GEZ: taken = a >= 0; break;
                    case Instruction::IF_GTZ: taken = a > 0; break;
                    default:                  taken = a <= 0; break;
                }
                if (taken) {
                    dex_pc += inst->VRegB_21t();
                    continue;
                }
                break;
            }

            // ── an operand ──
            case Instruction::NEG_INT:
                frame->SetInt(inst->VRegA_12x(), -frame->GetInt(inst->VRegB_12x()));
                break;
            case Instruction::NOT_INT:
                frame->SetInt(inst->VRegA_12x(), ~frame->GetInt(inst->VRegB_12x()));
                break;
            case Instruction::NEG_LONG:
                frame->SetLong(inst->VRegA_12x(), -frame->GetLong(inst->VRegB_12x()));
                break;
            case Instruction::NOT_LONG:
                frame->SetLong(inst->VRegA_12x(), ~frame->GetLong(inst->VRegB_12x()));
                break;
            case Instruction::NEG_FLOAT:
                frame->SetFloat(inst->VRegA_12x(), -frame->GetFloat(inst->VRegB_12x()));
                break;
            case Instruction::NEG_DOUBLE:
                frame->SetDouble(inst->VRegA_12x(), -frame->GetDouble(inst->VRegB_12x()));
                break;

            // ── change style ──
            case Instruction::INT_TO_LONG:
                frame->SetLong(inst->VRegA_12x(), frame->GetInt(inst->VRegB_12x()));
                break;
            case Instruction::INT_TO_FLOAT:
                frame->SetFloat(inst->VRegA_12x(),
                                static_cast<float>(frame->GetInt(inst->VRegB_12x())));
                break;
            case Instruction::INT_TO_DOUBLE:
                frame->SetDouble(inst->VRegA_12x(), frame->GetInt(inst->VRegB_12x()));
                break;
            case Instruction::LONG_TO_INT:
                frame->SetInt(inst->VRegA_12x(),
                              static_cast<int32_t>(frame->GetLong(inst->VRegB_12x())));
                break;
            case Instruction::LONG_TO_FLOAT:
                frame->SetFloat(inst->VRegA_12x(),
                                static_cast<float>(frame->GetLong(inst->VRegB_12x())));
                break;
            case Instruction::LONG_TO_DOUBLE:
                frame->SetDouble(inst->VRegA_12x(),
                                 static_cast<double>(frame->GetLong(inst->VRegB_12x())));
                break;
            case Instruction::FLOAT_TO_INT:
                frame->SetInt(inst->VRegA_12x(),
                              JavaFloatToInt<int32_t>(frame->GetFloat(inst->VRegB_12x())));
                break;
            case Instruction::FLOAT_TO_LONG:
                frame->SetLong(inst->VRegA_12x(),
                               JavaFloatToInt<int64_t>(frame->GetFloat(inst->VRegB_12x())));
                break;
            case Instruction::FLOAT_TO_DOUBLE:
                frame->SetDouble(inst->VRegA_12x(), frame->GetFloat(inst->VRegB_12x()));
                break;
            case Instruction::DOUBLE_TO_INT:
                frame->SetInt(inst->VRegA_12x(),
                              JavaFloatToInt<int32_t>(frame->GetDouble(inst->VRegB_12x())));
                break;
            case Instruction::DOUBLE_TO_LONG:
                frame->SetLong(inst->VRegA_12x(),
                               JavaFloatToInt<int64_t>(frame->GetDouble(inst->VRegB_12x())));
                break;
            case Instruction::DOUBLE_TO_FLOAT:
                frame->SetFloat(inst->VRegA_12x(),
                                static_cast<float>(frame->GetDouble(inst->VRegB_12x())));
                break;
            case Instruction::INT_TO_BYTE:
                frame->SetInt(inst->VRegA_12x(),
                              static_cast<int8_t>(frame->GetInt(inst->VRegB_12x())));
                break;
            case Instruction::INT_TO_CHAR:
                frame->SetInt(inst->VRegA_12x(),
                              static_cast<uint16_t>(frame->GetInt(inst->VRegB_12x())));
                break;
            case Instruction::INT_TO_SHORT:
                frame->SetInt(inst->VRegA_12x(),
                              static_cast<int16_t>(frame->GetInt(inst->VRegB_12x())));
                break;

            // ── int arithmetic, two operands (23x) ──
            case Instruction::ADD_INT:
                frame->SetInt(inst->VRegA_23x(),
                              static_cast<int32_t>(
                                  static_cast<uint32_t>(frame->GetInt(inst->VRegB_23x())) +
                                  static_cast<uint32_t>(frame->GetInt(inst->VRegC_23x()))));
                break;
            case Instruction::SUB_INT:
                frame->SetInt(inst->VRegA_23x(),
                              static_cast<int32_t>(
                                  static_cast<uint32_t>(frame->GetInt(inst->VRegB_23x())) -
                                  static_cast<uint32_t>(frame->GetInt(inst->VRegC_23x()))));
                break;
            case Instruction::MUL_INT:
                frame->SetInt(inst->VRegA_23x(),
                              static_cast<int32_t>(
                                  static_cast<uint32_t>(frame->GetInt(inst->VRegB_23x())) *
                                  static_cast<uint32_t>(frame->GetInt(inst->VRegC_23x()))));
                break;
            case Instruction::DIV_INT: {
                const int32_t b = frame->GetInt(inst->VRegC_23x());
                if (b == 0) {
                    ThrowException("Ljava/lang/ArithmeticException;", "divide by zero");
                    return return_value;
                }
                frame->SetInt(inst->VRegA_23x(), JavaIntDiv(frame->GetInt(inst->VRegB_23x()), b));
                break;
            }
            case Instruction::REM_INT: {
                const int32_t b = frame->GetInt(inst->VRegC_23x());
                if (b == 0) {
                    ThrowException("Ljava/lang/ArithmeticException;", "divide by zero");
                    return return_value;
                }
                frame->SetInt(inst->VRegA_23x(), JavaIntRem(frame->GetInt(inst->VRegB_23x()), b));
                break;
            }
            case Instruction::AND_INT:
                frame->SetInt(inst->VRegA_23x(), frame->GetInt(inst->VRegB_23x()) &
                                                     frame->GetInt(inst->VRegC_23x()));
                break;
            case Instruction::OR_INT:
                frame->SetInt(inst->VRegA_23x(), frame->GetInt(inst->VRegB_23x()) |
                                                     frame->GetInt(inst->VRegC_23x()));
                break;
            case Instruction::XOR_INT:
                frame->SetInt(inst->VRegA_23x(), frame->GetInt(inst->VRegB_23x()) ^
                                                     frame->GetInt(inst->VRegC_23x()));
                break;
            case Instruction::SHL_INT:
                frame->SetInt(inst->VRegA_23x(), JavaShl(frame->GetInt(inst->VRegB_23x()),
                                                         frame->GetInt(inst->VRegC_23x())));
                break;
            case Instruction::SHR_INT:
                frame->SetInt(inst->VRegA_23x(), JavaShr(frame->GetInt(inst->VRegB_23x()),
                                                         frame->GetInt(inst->VRegC_23x())));
                break;
            case Instruction::USHR_INT:
                frame->SetInt(inst->VRegA_23x(), JavaUshr(frame->GetInt(inst->VRegB_23x()),
                                                          frame->GetInt(inst->VRegC_23x())));
                break;

            // ── long arithmetic ──
            case Instruction::ADD_LONG:
                frame->SetLong(inst->VRegA_23x(),
                               static_cast<int64_t>(
                                   static_cast<uint64_t>(frame->GetLong(inst->VRegB_23x())) +
                                   static_cast<uint64_t>(frame->GetLong(inst->VRegC_23x()))));
                break;
            case Instruction::SUB_LONG:
                frame->SetLong(inst->VRegA_23x(),
                               static_cast<int64_t>(
                                   static_cast<uint64_t>(frame->GetLong(inst->VRegB_23x())) -
                                   static_cast<uint64_t>(frame->GetLong(inst->VRegC_23x()))));
                break;
            case Instruction::MUL_LONG:
                frame->SetLong(inst->VRegA_23x(),
                               static_cast<int64_t>(
                                   static_cast<uint64_t>(frame->GetLong(inst->VRegB_23x())) *
                                   static_cast<uint64_t>(frame->GetLong(inst->VRegC_23x()))));
                break;
            case Instruction::DIV_LONG: {
                const int64_t b = frame->GetLong(inst->VRegC_23x());
                if (b == 0) {
                    ThrowException("Ljava/lang/ArithmeticException;", "divide by zero");
                    return return_value;
                }
                frame->SetLong(inst->VRegA_23x(), JavaLongDiv(frame->GetLong(inst->VRegB_23x()), b));
                break;
            }
            case Instruction::REM_LONG: {
                const int64_t b = frame->GetLong(inst->VRegC_23x());
                if (b == 0) {
                    ThrowException("Ljava/lang/ArithmeticException;", "divide by zero");
                    return return_value;
                }
                frame->SetLong(inst->VRegA_23x(), JavaLongRem(frame->GetLong(inst->VRegB_23x()), b));
                break;
            }
            case Instruction::AND_LONG:
                frame->SetLong(inst->VRegA_23x(), frame->GetLong(inst->VRegB_23x()) &
                                                      frame->GetLong(inst->VRegC_23x()));
                break;
            case Instruction::OR_LONG:
                frame->SetLong(inst->VRegA_23x(), frame->GetLong(inst->VRegB_23x()) |
                                                      frame->GetLong(inst->VRegC_23x()));
                break;
            case Instruction::XOR_LONG:
                frame->SetLong(inst->VRegA_23x(), frame->GetLong(inst->VRegB_23x()) ^
                                                      frame->GetLong(inst->VRegC_23x()));
                break;
            case Instruction::SHL_LONG:
                frame->SetLong(inst->VRegA_23x(), JavaShlLong(frame->GetLong(inst->VRegB_23x()),
                                                              frame->GetInt(inst->VRegC_23x())));
                break;
            case Instruction::SHR_LONG:
                frame->SetLong(inst->VRegA_23x(), JavaShrLong(frame->GetLong(inst->VRegB_23x()),
                                                              frame->GetInt(inst->VRegC_23x())));
                break;
            case Instruction::USHR_LONG:
                frame->SetLong(inst->VRegA_23x(), JavaUshrLong(frame->GetLong(inst->VRegB_23x()),
                                                               frame->GetInt(inst->VRegC_23x())));
                break;

            // ── float/double arithmetic ──
            case Instruction::ADD_FLOAT:
                frame->SetFloat(inst->VRegA_23x(), frame->GetFloat(inst->VRegB_23x()) +
                                                       frame->GetFloat(inst->VRegC_23x()));
                break;
            case Instruction::SUB_FLOAT:
                frame->SetFloat(inst->VRegA_23x(), frame->GetFloat(inst->VRegB_23x()) -
                                                       frame->GetFloat(inst->VRegC_23x()));
                break;
            case Instruction::MUL_FLOAT:
                frame->SetFloat(inst->VRegA_23x(), frame->GetFloat(inst->VRegB_23x()) *
                                                       frame->GetFloat(inst->VRegC_23x()));
                break;
            case Instruction::DIV_FLOAT:
                frame->SetFloat(inst->VRegA_23x(), frame->GetFloat(inst->VRegB_23x()) /
                                                       frame->GetFloat(inst->VRegC_23x()));
                break;
            case Instruction::REM_FLOAT:
                frame->SetFloat(inst->VRegA_23x(), std::fmod(frame->GetFloat(inst->VRegB_23x()),
                                                             frame->GetFloat(inst->VRegC_23x())));
                break;
            case Instruction::ADD_DOUBLE:
                frame->SetDouble(inst->VRegA_23x(), frame->GetDouble(inst->VRegB_23x()) +
                                                        frame->GetDouble(inst->VRegC_23x()));
                break;
            case Instruction::SUB_DOUBLE:
                frame->SetDouble(inst->VRegA_23x(), frame->GetDouble(inst->VRegB_23x()) -
                                                        frame->GetDouble(inst->VRegC_23x()));
                break;
            case Instruction::MUL_DOUBLE:
                frame->SetDouble(inst->VRegA_23x(), frame->GetDouble(inst->VRegB_23x()) *
                                                        frame->GetDouble(inst->VRegC_23x()));
                break;
            case Instruction::DIV_DOUBLE:
                frame->SetDouble(inst->VRegA_23x(), frame->GetDouble(inst->VRegB_23x()) /
                                                        frame->GetDouble(inst->VRegC_23x()));
                break;
            case Instruction::REM_DOUBLE:
                frame->SetDouble(inst->VRegA_23x(), std::fmod(frame->GetDouble(inst->VRegB_23x()),
                                                              frame->GetDouble(inst->VRegC_23x())));
                break;

            // ── form /2addr: destination is also the first operand ──
            case Instruction::ADD_INT_2ADDR:
                frame->SetInt(inst->VRegA_12x(),
                              static_cast<int32_t>(
                                  static_cast<uint32_t>(frame->GetInt(inst->VRegA_12x())) +
                                  static_cast<uint32_t>(frame->GetInt(inst->VRegB_12x()))));
                break;
            case Instruction::SUB_INT_2ADDR:
                frame->SetInt(inst->VRegA_12x(),
                              static_cast<int32_t>(
                                  static_cast<uint32_t>(frame->GetInt(inst->VRegA_12x())) -
                                  static_cast<uint32_t>(frame->GetInt(inst->VRegB_12x()))));
                break;
            case Instruction::MUL_INT_2ADDR:
                frame->SetInt(inst->VRegA_12x(),
                              static_cast<int32_t>(
                                  static_cast<uint32_t>(frame->GetInt(inst->VRegA_12x())) *
                                  static_cast<uint32_t>(frame->GetInt(inst->VRegB_12x()))));
                break;
            case Instruction::DIV_INT_2ADDR: {
                const int32_t b = frame->GetInt(inst->VRegB_12x());
                if (b == 0) {
                    ThrowException("Ljava/lang/ArithmeticException;", "divide by zero");
                    return return_value;
                }
                frame->SetInt(inst->VRegA_12x(), JavaIntDiv(frame->GetInt(inst->VRegA_12x()), b));
                break;
            }
            case Instruction::REM_INT_2ADDR: {
                const int32_t b = frame->GetInt(inst->VRegB_12x());
                if (b == 0) {
                    ThrowException("Ljava/lang/ArithmeticException;", "divide by zero");
                    return return_value;
                }
                frame->SetInt(inst->VRegA_12x(), JavaIntRem(frame->GetInt(inst->VRegA_12x()), b));
                break;
            }
            case Instruction::AND_INT_2ADDR:
                frame->SetInt(inst->VRegA_12x(),
                              frame->GetInt(inst->VRegA_12x()) & frame->GetInt(inst->VRegB_12x()));
                break;
            case Instruction::OR_INT_2ADDR:
                frame->SetInt(inst->VRegA_12x(),
                              frame->GetInt(inst->VRegA_12x()) | frame->GetInt(inst->VRegB_12x()));
                break;
            case Instruction::XOR_INT_2ADDR:
                frame->SetInt(inst->VRegA_12x(),
                              frame->GetInt(inst->VRegA_12x()) ^ frame->GetInt(inst->VRegB_12x()));
                break;
            case Instruction::SHL_INT_2ADDR:
                frame->SetInt(inst->VRegA_12x(), JavaShl(frame->GetInt(inst->VRegA_12x()),
                                                         frame->GetInt(inst->VRegB_12x())));
                break;
            case Instruction::SHR_INT_2ADDR:
                frame->SetInt(inst->VRegA_12x(), JavaShr(frame->GetInt(inst->VRegA_12x()),
                                                         frame->GetInt(inst->VRegB_12x())));
                break;
            case Instruction::USHR_INT_2ADDR:
                frame->SetInt(inst->VRegA_12x(), JavaUshr(frame->GetInt(inst->VRegA_12x()),
                                                          frame->GetInt(inst->VRegB_12x())));
                break;
            case Instruction::ADD_LONG_2ADDR:
                frame->SetLong(inst->VRegA_12x(),
                               static_cast<int64_t>(
                                   static_cast<uint64_t>(frame->GetLong(inst->VRegA_12x())) +
                                   static_cast<uint64_t>(frame->GetLong(inst->VRegB_12x()))));
                break;
            case Instruction::SUB_LONG_2ADDR:
                frame->SetLong(inst->VRegA_12x(),
                               static_cast<int64_t>(
                                   static_cast<uint64_t>(frame->GetLong(inst->VRegA_12x())) -
                                   static_cast<uint64_t>(frame->GetLong(inst->VRegB_12x()))));
                break;
            case Instruction::MUL_LONG_2ADDR:
                frame->SetLong(inst->VRegA_12x(),
                               static_cast<int64_t>(
                                   static_cast<uint64_t>(frame->GetLong(inst->VRegA_12x())) *
                                   static_cast<uint64_t>(frame->GetLong(inst->VRegB_12x()))));
                break;
            case Instruction::DIV_LONG_2ADDR: {
                const int64_t b = frame->GetLong(inst->VRegB_12x());
                if (b == 0) {
                    ThrowException("Ljava/lang/ArithmeticException;", "divide by zero");
                    return return_value;
                }
                frame->SetLong(inst->VRegA_12x(), JavaLongDiv(frame->GetLong(inst->VRegA_12x()), b));
                break;
            }
            case Instruction::REM_LONG_2ADDR: {
                const int64_t b = frame->GetLong(inst->VRegB_12x());
                if (b == 0) {
                    ThrowException("Ljava/lang/ArithmeticException;", "divide by zero");
                    return return_value;
                }
                frame->SetLong(inst->VRegA_12x(), JavaLongRem(frame->GetLong(inst->VRegA_12x()), b));
                break;
            }
            case Instruction::AND_LONG_2ADDR:
                frame->SetLong(inst->VRegA_12x(), frame->GetLong(inst->VRegA_12x()) &
                                                      frame->GetLong(inst->VRegB_12x()));
                break;
            case Instruction::OR_LONG_2ADDR:
                frame->SetLong(inst->VRegA_12x(), frame->GetLong(inst->VRegA_12x()) |
                                                      frame->GetLong(inst->VRegB_12x()));
                break;
            case Instruction::XOR_LONG_2ADDR:
                frame->SetLong(inst->VRegA_12x(), frame->GetLong(inst->VRegA_12x()) ^
                                                      frame->GetLong(inst->VRegB_12x()));
                break;
            case Instruction::SHL_LONG_2ADDR:
                frame->SetLong(inst->VRegA_12x(), JavaShlLong(frame->GetLong(inst->VRegA_12x()),
                                                              frame->GetInt(inst->VRegB_12x())));
                break;
            case Instruction::SHR_LONG_2ADDR:
                frame->SetLong(inst->VRegA_12x(), JavaShrLong(frame->GetLong(inst->VRegA_12x()),
                                                              frame->GetInt(inst->VRegB_12x())));
                break;
            case Instruction::USHR_LONG_2ADDR:
                frame->SetLong(inst->VRegA_12x(), JavaUshrLong(frame->GetLong(inst->VRegA_12x()),
                                                               frame->GetInt(inst->VRegB_12x())));
                break;
            case Instruction::ADD_FLOAT_2ADDR:
                frame->SetFloat(inst->VRegA_12x(), frame->GetFloat(inst->VRegA_12x()) +
                                                       frame->GetFloat(inst->VRegB_12x()));
                break;
            case Instruction::SUB_FLOAT_2ADDR:
                frame->SetFloat(inst->VRegA_12x(), frame->GetFloat(inst->VRegA_12x()) -
                                                       frame->GetFloat(inst->VRegB_12x()));
                break;
            case Instruction::MUL_FLOAT_2ADDR:
                frame->SetFloat(inst->VRegA_12x(), frame->GetFloat(inst->VRegA_12x()) *
                                                       frame->GetFloat(inst->VRegB_12x()));
                break;
            case Instruction::DIV_FLOAT_2ADDR:
                frame->SetFloat(inst->VRegA_12x(), frame->GetFloat(inst->VRegA_12x()) /
                                                       frame->GetFloat(inst->VRegB_12x()));
                break;
            case Instruction::REM_FLOAT_2ADDR:
                frame->SetFloat(inst->VRegA_12x(), std::fmod(frame->GetFloat(inst->VRegA_12x()),
                                                             frame->GetFloat(inst->VRegB_12x())));
                break;
            case Instruction::ADD_DOUBLE_2ADDR:
                frame->SetDouble(inst->VRegA_12x(), frame->GetDouble(inst->VRegA_12x()) +
                                                        frame->GetDouble(inst->VRegB_12x()));
                break;
            case Instruction::SUB_DOUBLE_2ADDR:
                frame->SetDouble(inst->VRegA_12x(), frame->GetDouble(inst->VRegA_12x()) -
                                                        frame->GetDouble(inst->VRegB_12x()));
                break;
            case Instruction::MUL_DOUBLE_2ADDR:
                frame->SetDouble(inst->VRegA_12x(), frame->GetDouble(inst->VRegA_12x()) *
                                                        frame->GetDouble(inst->VRegB_12x()));
                break;
            case Instruction::DIV_DOUBLE_2ADDR:
                frame->SetDouble(inst->VRegA_12x(), frame->GetDouble(inst->VRegA_12x()) /
                                                        frame->GetDouble(inst->VRegB_12x()));
                break;
            case Instruction::REM_DOUBLE_2ADDR:
                frame->SetDouble(inst->VRegA_12x(), std::fmod(frame->GetDouble(inst->VRegA_12x()),
                                                              frame->GetDouble(inst->VRegB_12x())));
                break;

            // ── int with constant ──
            case Instruction::ADD_INT_LIT16:
                frame->SetInt(inst->VRegA_22s(),
                              static_cast<int32_t>(
                                  static_cast<uint32_t>(frame->GetInt(inst->VRegB_22s())) +
                                  static_cast<uint32_t>(inst->VRegC_22s())));
                break;
            case Instruction::RSUB_INT:
                frame->SetInt(inst->VRegA_22s(),
                              static_cast<int32_t>(
                                  static_cast<uint32_t>(inst->VRegC_22s()) -
                                  static_cast<uint32_t>(frame->GetInt(inst->VRegB_22s()))));
                break;
            case Instruction::MUL_INT_LIT16:
                frame->SetInt(inst->VRegA_22s(),
                              static_cast<int32_t>(
                                  static_cast<uint32_t>(frame->GetInt(inst->VRegB_22s())) *
                                  static_cast<uint32_t>(inst->VRegC_22s())));
                break;
            case Instruction::DIV_INT_LIT16: {
                const int32_t b = inst->VRegC_22s();
                if (b == 0) {
                    ThrowException("Ljava/lang/ArithmeticException;", "divide by zero");
                    return return_value;
                }
                frame->SetInt(inst->VRegA_22s(), JavaIntDiv(frame->GetInt(inst->VRegB_22s()), b));
                break;
            }
            case Instruction::REM_INT_LIT16: {
                const int32_t b = inst->VRegC_22s();
                if (b == 0) {
                    ThrowException("Ljava/lang/ArithmeticException;", "divide by zero");
                    return return_value;
                }
                frame->SetInt(inst->VRegA_22s(), JavaIntRem(frame->GetInt(inst->VRegB_22s()), b));
                break;
            }
            case Instruction::AND_INT_LIT16:
                frame->SetInt(inst->VRegA_22s(),
                              frame->GetInt(inst->VRegB_22s()) & inst->VRegC_22s());
                break;
            case Instruction::OR_INT_LIT16:
                frame->SetInt(inst->VRegA_22s(),
                              frame->GetInt(inst->VRegB_22s()) | inst->VRegC_22s());
                break;
            case Instruction::XOR_INT_LIT16:
                frame->SetInt(inst->VRegA_22s(),
                              frame->GetInt(inst->VRegB_22s()) ^ inst->VRegC_22s());
                break;

            case Instruction::ADD_INT_LIT8:
                frame->SetInt(inst->VRegA_22b(),
                              static_cast<int32_t>(
                                  static_cast<uint32_t>(frame->GetInt(inst->VRegB_22b())) +
                                  static_cast<uint32_t>(inst->VRegC_22b())));
                break;
            case Instruction::RSUB_INT_LIT8:
                frame->SetInt(inst->VRegA_22b(),
                              static_cast<int32_t>(
                                  static_cast<uint32_t>(inst->VRegC_22b()) -
                                  static_cast<uint32_t>(frame->GetInt(inst->VRegB_22b()))));
                break;
            case Instruction::MUL_INT_LIT8:
                frame->SetInt(inst->VRegA_22b(),
                              static_cast<int32_t>(
                                  static_cast<uint32_t>(frame->GetInt(inst->VRegB_22b())) *
                                  static_cast<uint32_t>(inst->VRegC_22b())));
                break;
            case Instruction::DIV_INT_LIT8: {
                const int32_t b = inst->VRegC_22b();
                if (b == 0) {
                    ThrowException("Ljava/lang/ArithmeticException;", "divide by zero");
                    return return_value;
                }
                frame->SetInt(inst->VRegA_22b(), JavaIntDiv(frame->GetInt(inst->VRegB_22b()), b));
                break;
            }
            case Instruction::REM_INT_LIT8: {
                const int32_t b = inst->VRegC_22b();
                if (b == 0) {
                    ThrowException("Ljava/lang/ArithmeticException;", "divide by zero");
                    return return_value;
                }
                frame->SetInt(inst->VRegA_22b(), JavaIntRem(frame->GetInt(inst->VRegB_22b()), b));
                break;
            }
            case Instruction::AND_INT_LIT8:
                frame->SetInt(inst->VRegA_22b(),
                              frame->GetInt(inst->VRegB_22b()) & inst->VRegC_22b());
                break;
            case Instruction::OR_INT_LIT8:
                frame->SetInt(inst->VRegA_22b(),
                              frame->GetInt(inst->VRegB_22b()) | inst->VRegC_22b());
                break;
            case Instruction::XOR_INT_LIT8:
                frame->SetInt(inst->VRegA_22b(),
                              frame->GetInt(inst->VRegB_22b()) ^ inst->VRegC_22b());
                break;
            case Instruction::SHL_INT_LIT8:
                frame->SetInt(inst->VRegA_22b(),
                              JavaShl(frame->GetInt(inst->VRegB_22b()), inst->VRegC_22b()));
                break;
            case Instruction::SHR_INT_LIT8:
                frame->SetInt(inst->VRegA_22b(),
                              JavaShr(frame->GetInt(inst->VRegB_22b()), inst->VRegC_22b()));
                break;
            case Instruction::USHR_INT_LIT8:
                frame->SetInt(inst->VRegA_22b(),
                              JavaUshr(frame->GetInt(inst->VRegB_22b()), inst->VRegC_22b()));
                break;

            // ── constant string and constant class ──
            case Instruction::CONST_STRING: {
                const char* s = method->dex_file->StringDataByIdx(
                    art::dex::StringIndex(inst->VRegB_21c()));
                frame->SetRef(inst->VRegA_21c(), linker_->InternString(s));
                break;
            }
            case Instruction::CONST_STRING_JUMBO: {
                const char* s = method->dex_file->StringDataByIdx(
                    art::dex::StringIndex(inst->VRegB_31c()));
                frame->SetRef(inst->VRegA_31c(), linker_->InternString(s));
                break;
            }
            case Instruction::CONST_CLASS: {
                DexClass* klass = ResolveClass(method, inst->VRegB_21c());
                if (klass == nullptr) {
                    ThrowException("Ljava/lang/ClassNotFoundException;", "const-class");
                    return return_value;
                }
                frame->SetRef(inst->VRegA_21c(), linker_->GetClassObject(klass));
                break;
            }

            // ── create objects and arrays ──
            case Instruction::NEW_INSTANCE: {
                DexClass* klass = ResolveClass(method, inst->VRegB_21c());
                if (klass == nullptr) {
                    ThrowException("Ljava/lang/ClassNotFoundException;", "new-instance");
                    return return_value;
                }
                EnsureInitialized(klass);
                if (HasPendingException()) return return_value;
                DexObject* obj = linker_->AllocObject(klass);
                if (obj == nullptr) {
                    ThrowException("Ljava/lang/OutOfMemoryError;", klass->PrettyName());
                    return return_value;
                }
                frame->SetRef(inst->VRegA_21c(), obj);
                break;
            }
            case Instruction::NEW_ARRAY: {
                const int32_t length = frame->GetInt(inst->VRegB_22c());
                if (length < 0) {
                    ThrowException("Ljava/lang/NegativeArraySizeException;",
                                   std::to_string(length));
                    return return_value;
                }
                DexClass* array_class = ResolveClass(method, inst->VRegC_22c());
                if (array_class == nullptr) {
                    ThrowException("Ljava/lang/ClassNotFoundException;", "new-array");
                    return return_value;
                }
                DexArray* arr = linker_->AllocArray(array_class, length);
                if (arr == nullptr) {
                    ThrowException("Ljava/lang/OutOfMemoryError;", "new-array");
                    return return_value;
                }
                frame->SetRef(inst->VRegA_22c(), arr);
                break;
            }
            case Instruction::ARRAY_LENGTH: {
                auto* arr = static_cast<DexArray*>(frame->GetRef(inst->VRegB_12x()));
                if (arr == nullptr) {
                    ThrowException("Ljava/lang/NullPointerException;", "array-length on null");
                    return return_value;
                }
                frame->SetInt(inst->VRegA_12x(), arr->length);
                break;
            }

            // ── check the type ──
            case Instruction::INSTANCE_OF: {
                DexObject* obj = frame->GetRef(inst->VRegB_22c());
                DexClass* target = ResolveClass(method, inst->VRegC_22c());
                const bool ok = obj != nullptr && obj->clazz != nullptr && target != nullptr &&
                                obj->clazz->IsSubClassOf(target);
                frame->SetInt(inst->VRegA_22c(), ok ? 1 : 0);
                break;
            }
            case Instruction::CHECK_CAST: {
                DexObject* obj = frame->GetRef(inst->VRegA_21c());
                if (obj == nullptr) break;  // cast null is always valid
                DexClass* target = ResolveClass(method, inst->VRegB_21c());
                if (target == nullptr || obj->clazz == nullptr ||
                    !obj->clazz->IsSubClassOf(target)) {
                    ThrowException("Ljava/lang/ClassCastException;",
                                   (obj->clazz != nullptr ? obj->clazz->PrettyName() : "?") +
                                       " not " +
                                       (target != nullptr ? target->PrettyName() : "?"));
                    return return_value;
                }
                break;
            }

            // ── field instance ──
            case Instruction::IGET:
            case Instruction::IGET_WIDE:
            case Instruction::IGET_OBJECT:
            case Instruction::IGET_BOOLEAN:
            case Instruction::IGET_BYTE:
            case Instruction::IGET_CHAR:
            case Instruction::IGET_SHORT: {
                DexObject* obj = frame->GetRef(inst->VRegB_22c());
                if (obj == nullptr) {
                    ThrowException("Ljava/lang/NullPointerException;", "iget on null");
                    return return_value;
                }
                DexField* field = ResolveField(method, inst->VRegC_22c(), /*is_static=*/false);
                if (field == nullptr) {
                    ThrowException("Ljava/lang/NoSuchFieldError;", "iget");
                    return return_value;
                }
                const uint32_t off = field->offset_or_slot;
                const uint32_t vreg = inst->VRegA_22c();
                switch (inst->Opcode()) {
                    case Instruction::IGET_BOOLEAN:
                    case Instruction::IGET_BYTE:
                        frame->SetInt(vreg, obj->GetField<int8_t>(off));
                        break;
                    case Instruction::IGET_CHAR:
                        frame->SetInt(vreg, obj->GetField<uint16_t>(off));
                        break;
                    case Instruction::IGET_SHORT:
                        frame->SetInt(vreg, obj->GetField<int16_t>(off));
                        break;
                    case Instruction::IGET_WIDE:
                        frame->SetLong(vreg, obj->GetField<int64_t>(off));
                        break;
                    case Instruction::IGET_OBJECT:
                        frame->SetRef(vreg, obj->GetField<DexObject*>(off));
                        break;
                    default:
                        frame->SetInt(vreg, obj->GetField<int32_t>(off));
                        break;
                }
                break;
            }

            case Instruction::IPUT:
            case Instruction::IPUT_WIDE:
            case Instruction::IPUT_OBJECT:
            case Instruction::IPUT_BOOLEAN:
            case Instruction::IPUT_BYTE:
            case Instruction::IPUT_CHAR:
            case Instruction::IPUT_SHORT: {
                DexObject* obj = frame->GetRef(inst->VRegB_22c());
                if (obj == nullptr) {
                    ThrowException("Ljava/lang/NullPointerException;", "iput on null");
                    return return_value;
                }
                DexField* field = ResolveField(method, inst->VRegC_22c(), /*is_static=*/false);
                if (field == nullptr) {
                    ThrowException("Ljava/lang/NoSuchFieldError;", "iput");
                    return return_value;
                }
                const uint32_t off = field->offset_or_slot;
                const uint32_t vreg = inst->VRegA_22c();
                switch (inst->Opcode()) {
                    case Instruction::IPUT_BOOLEAN:
                    case Instruction::IPUT_BYTE:
                        obj->SetField<int8_t>(off, static_cast<int8_t>(frame->GetInt(vreg)));
                        break;
                    case Instruction::IPUT_CHAR:
                        obj->SetField<uint16_t>(off, static_cast<uint16_t>(frame->GetInt(vreg)));
                        break;
                    case Instruction::IPUT_SHORT:
                        obj->SetField<int16_t>(off, static_cast<int16_t>(frame->GetInt(vreg)));
                        break;
                    case Instruction::IPUT_WIDE:
                        obj->SetField<int64_t>(off, frame->GetLong(vreg));
                        break;
                    case Instruction::IPUT_OBJECT:
                        obj->SetField<DexObject*>(off, frame->GetRef(vreg));
                        break;
                    default:
                        obj->SetField<int32_t>(off, frame->GetInt(vreg));
                        break;
                }
                break;
            }

            // ── field static ──
            case Instruction::SGET:
            case Instruction::SGET_WIDE:
            case Instruction::SGET_OBJECT:
            case Instruction::SGET_BOOLEAN:
            case Instruction::SGET_BYTE:
            case Instruction::SGET_CHAR:
            case Instruction::SGET_SHORT: {
                DexField* field = ResolveField(method, inst->VRegB_21c(), /*is_static=*/true);
                if (field == nullptr || field->declaring_class == nullptr) {
                    ThrowException("Ljava/lang/NoSuchFieldError;", "sget");
                    return return_value;
                }
                EnsureInitialized(field->declaring_class);
                if (HasPendingException()) return return_value;
                auto& values = field->declaring_class->static_values;
                const uint32_t slot = field->offset_or_slot;
                if (slot >= values.size()) {
                    ThrowException("Ljava/lang/NoSuchFieldError;", "sget slot out of range");
                    return return_value;
                }
                frame->Set(inst->VRegA_21c(), values[slot]);
                break;
            }

            case Instruction::SPUT:
            case Instruction::SPUT_WIDE:
            case Instruction::SPUT_OBJECT:
            case Instruction::SPUT_BOOLEAN:
            case Instruction::SPUT_BYTE:
            case Instruction::SPUT_CHAR:
            case Instruction::SPUT_SHORT: {
                DexField* field = ResolveField(method, inst->VRegB_21c(), /*is_static=*/true);
                if (field == nullptr || field->declaring_class == nullptr) {
                    ThrowException("Ljava/lang/NoSuchFieldError;", "sput");
                    return return_value;
                }
                EnsureInitialized(field->declaring_class);
                if (HasPendingException()) return return_value;
                auto& values = field->declaring_class->static_values;
                const uint32_t slot = field->offset_or_slot;
                if (slot >= values.size()) {
                    ThrowException("Ljava/lang/NoSuchFieldError;", "sput slot out of range");
                    return return_value;
                }
                values[slot] = frame->Get(inst->VRegA_21c());
                break;
            }

            // ── array element ──
            case Instruction::AGET:
            case Instruction::AGET_WIDE:
            case Instruction::AGET_OBJECT:
            case Instruction::AGET_BOOLEAN:
            case Instruction::AGET_BYTE:
            case Instruction::AGET_CHAR:
            case Instruction::AGET_SHORT: {
                auto* arr = static_cast<DexArray*>(frame->GetRef(inst->VRegB_23x()));
                if (arr == nullptr) {
                    ThrowException("Ljava/lang/NullPointerException;", "aget on null");
                    return return_value;
                }
                const int32_t index = frame->GetInt(inst->VRegC_23x());
                if (index < 0 || index >= arr->length) {
                    ThrowException("Ljava/lang/ArrayIndexOutOfBoundsException;",
                                   "index " + std::to_string(index) + " length " +
                                       std::to_string(arr->length));
                    return return_value;
                }
                const uint32_t vreg = inst->VRegA_23x();
                switch (inst->Opcode()) {
                    case Instruction::AGET_BOOLEAN:
                    case Instruction::AGET_BYTE:
                        frame->SetInt(vreg, arr->Get<int8_t>(index));
                        break;
                    case Instruction::AGET_CHAR:
                        frame->SetInt(vreg, arr->Get<uint16_t>(index));
                        break;
                    case Instruction::AGET_SHORT:
                        frame->SetInt(vreg, arr->Get<int16_t>(index));
                        break;
                    case Instruction::AGET_WIDE:
                        frame->SetLong(vreg, arr->Get<int64_t>(index));
                        break;
                    case Instruction::AGET_OBJECT:
                        frame->SetRef(vreg, arr->Get<DexObject*>(index));
                        break;
                    default:
                        frame->SetInt(vreg, arr->Get<int32_t>(index));
                        break;
                }
                break;
            }

            case Instruction::APUT:
            case Instruction::APUT_WIDE:
            case Instruction::APUT_OBJECT:
            case Instruction::APUT_BOOLEAN:
            case Instruction::APUT_BYTE:
            case Instruction::APUT_CHAR:
            case Instruction::APUT_SHORT: {
                auto* arr = static_cast<DexArray*>(frame->GetRef(inst->VRegB_23x()));
                if (arr == nullptr) {
                    ThrowException("Ljava/lang/NullPointerException;", "aput on null");
                    return return_value;
                }
                const int32_t index = frame->GetInt(inst->VRegC_23x());
                if (index < 0 || index >= arr->length) {
                    ThrowException("Ljava/lang/ArrayIndexOutOfBoundsException;",
                                   "index " + std::to_string(index) + " length " +
                                       std::to_string(arr->length));
                    return return_value;
                }
                const uint32_t vreg = inst->VRegA_23x();
                switch (inst->Opcode()) {
                    case Instruction::APUT_BOOLEAN:
                    case Instruction::APUT_BYTE:
                        arr->Set<int8_t>(index, static_cast<int8_t>(frame->GetInt(vreg)));
                        break;
                    case Instruction::APUT_CHAR:
                        arr->Set<uint16_t>(index, static_cast<uint16_t>(frame->GetInt(vreg)));
                        break;
                    case Instruction::APUT_SHORT:
                        arr->Set<int16_t>(index, static_cast<int16_t>(frame->GetInt(vreg)));
                        break;
                    case Instruction::APUT_WIDE:
                        arr->Set<int64_t>(index, frame->GetLong(vreg));
                        break;
                    case Instruction::APUT_OBJECT:
                        arr->Set<DexObject*>(index, frame->GetRef(vreg));
                        break;
                    default:
                        arr->Set<int32_t>(index, frame->GetInt(vreg));
                        break;
                }
                break;
            }

            // ── call method ──
            case Instruction::INVOKE_VIRTUAL:
            case Instruction::INVOKE_SUPER:
            case Instruction::INVOKE_DIRECT:
            case Instruction::INVOKE_STATIC:
            case Instruction::INVOKE_INTERFACE:
                if (!InvokeMethod(frame, inst, /*is_range=*/false, inst->Opcode())) {
                    return return_value;
                }
                break;
            case Instruction::INVOKE_VIRTUAL_RANGE:
            case Instruction::INVOKE_SUPER_RANGE:
            case Instruction::INVOKE_DIRECT_RANGE:
            case Instruction::INVOKE_STATIC_RANGE:
            case Instruction::INVOKE_INTERFACE_RANGE:
                if (!InvokeMethod(frame, inst, /*is_range=*/true, inst->Opcode())) {
                    return return_value;
                }
                break;

            // ── switch ──
            case Instruction::PACKED_SWITCH: {
                const int32_t test = frame->GetInt(inst->VRegA_31t());
                const uint16_t* payload = insns + dex_pc + inst->VRegB_31t();
                const auto* ps =
                    reinterpret_cast<const Instruction::PackedSwitchPayload*>(payload);
                const int32_t delta = test - ps->first_key;
                if (delta >= 0 && delta < static_cast<int32_t>(ps->case_count)) {
                    dex_pc += ps->targets[delta];
                    continue;
                }
                break;
            }
            case Instruction::SPARSE_SWITCH: {
                const int32_t test = frame->GetInt(inst->VRegA_31t());
                const uint16_t* payload = insns + dex_pc + inst->VRegB_31t();
                const auto* ss =
                    reinterpret_cast<const Instruction::SparseSwitchPayload*>(payload);
                const int32_t* keys = ss->GetKeys();
                const int32_t* targets = ss->GetTargets();
                bool matched = false;
                for (uint16_t i = 0; i < ss->case_count; ++i) {
                    if (keys[i] == test) {
                        dex_pc += targets[i];
                        matched = true;
                        break;
                    }
                }
                if (matched) continue;
                break;
            }

            case Instruction::FILL_ARRAY_DATA: {
                auto* arr = static_cast<DexArray*>(frame->GetRef(inst->VRegA_31t()));
                if (arr == nullptr) {
                    ThrowException("Ljava/lang/NullPointerException;", "fill-array-data on null");
                    return return_value;
                }
                const uint16_t* payload = insns + dex_pc + inst->VRegB_31t();
                const auto* ad = reinterpret_cast<const Instruction::ArrayDataPayload*>(payload);
                if (static_cast<int32_t>(ad->element_count) > arr->length) {
                    ThrowException("Ljava/lang/ArrayIndexOutOfBoundsException;",
                                   "fill-array-data is longer than array");
                    return return_value;
                }
                std::memcpy(arr->Data(), ad->data,
                            static_cast<size_t>(ad->element_count) * ad->element_width);
                break;
            }

            // ── exception ──
            case Instruction::THROW: {
                DexObject* ex = frame->GetRef(inst->VRegA_11x());
                if (ex == nullptr) {
                    ThrowException("Ljava/lang/NullPointerException;", "throw null");
                    return return_value;
                }
                pending_exception_ = ex;
                last_error_ = std::string("throw ") +
                              (ex->clazz != nullptr ? ex->clazz->PrettyName() : "?");
                return return_value;
            }

            // There are no real threads so the monitor only counts; enough for single-thread code
            // and don't make mistakes, the code is correct.
            case Instruction::MONITOR_ENTER: {
                DexObject* obj = frame->GetRef(inst->VRegA_11x());
                if (obj == nullptr) {
                    ThrowException("Ljava/lang/NullPointerException;", "monitor-enter on null");
                    return return_value;
                }
                Monitor::Enter(obj);
                break;
            }
            case Instruction::MONITOR_EXIT: {
                DexObject* obj = frame->GetRef(inst->VRegA_11x());
                if (obj == nullptr) {
                    ThrowException("Ljava/lang/NullPointerException;", "monitor-exit on null");
                    return return_value;
                }
                if (!Monitor::Exit(obj)) {
                    ThrowException("Ljava/lang/IllegalMonitorStateException;",
                                   "monitor-exit without owning the monitor");
                    return return_value;
                }
                break;
            }

            default:
                ThrowException("Ljava/lang/UnsupportedOperationException;",
                               std::string("opcode not yet implemented: ") + inst->Name());
                return return_value;
        }
        if (HasPendingException()) return return_value;
        dex_pc = next_pc;
    }

    // Falling out of the end of the method without returning — bytecode error.
    ThrowException("Ljava/lang/VerifyError;", "run out of bytecode without return");
    return return_value;
}

}  // namespace kuart
}  // namespace kudroid
