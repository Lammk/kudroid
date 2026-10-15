// interpreter call frame: register array + current bytecode PC.
// Each slot is 64-bit, so wide values fit in one slot; the second slot stays empty.
#ifndef KUDROID_KUART_DEXFRAME_H
#define KUDROID_KUART_DEXFRAME_H

#include <cstdint>
#include <vector>

#include "kudroid/kuart/DexClass.h"
#include "kudroid/kuart/DexValue.h"

namespace kudroid {
namespace kuart {

class DexFrame {
public:
    explicit DexFrame(const DexMethod* method)
        : method_(method), registers_(method != nullptr ? method->registers_size : 0) {}

    const DexMethod* method() const { return method_; }

    size_t NumRegisters() const { return registers_.size(); }

    DexValue Get(uint32_t vreg) const {
        return vreg < registers_.size() ? registers_[vreg] : DexValue();
    }
    void Set(uint32_t vreg, DexValue v) {
        if (vreg < registers_.size()) registers_[vreg] = v;
    }

    // The register array itself, for compiled code.
    //
    // The JIT addresses virtual registers as offsets from this pointer rather than
    // keeping them in machine registers, which is what lets compiled code and the
    // interpreter share a frame: either can run at any instruction boundary and see the
    // state the other left.
    DexValue* registers() { return registers_.data(); }
    const DexValue* registers() const { return registers_.data(); }

    int32_t GetInt(uint32_t vreg) const { return Get(vreg).i; }
    int64_t GetLong(uint32_t vreg) const { return Get(vreg).j; }
    float GetFloat(uint32_t vreg) const { return Get(vreg).f; }
    double GetDouble(uint32_t vreg) const { return Get(vreg).d; }
    DexObject* GetRef(uint32_t vreg) const { return Get(vreg).l; }

    void SetInt(uint32_t vreg, int32_t v) { Set(vreg, DexValue::Int(v)); }
    void SetLong(uint32_t vreg, int64_t v) { Set(vreg, DexValue::Long(v)); }
    void SetFloat(uint32_t vreg, float v) { Set(vreg, DexValue::Float(v)); }
    void SetDouble(uint32_t vreg, double v) { Set(vreg, DexValue::Double(v)); }
    void SetRef(uint32_t vreg, DexObject* v) { Set(vreg, DexValue::Ref(v)); }

    // Args live in the frame's last registers (DEX convention), including
    // `this` for instance methods.
    uint32_t FirstArgRegister() const {
        if (method_ == nullptr) return 0;
        const uint32_t regs = method_->registers_size;
        const uint32_t ins = method_->ins_size;
        return regs >= ins ? regs - ins : 0;
    }

    // Load args into the last registers; wide values count as one arg but use two slots.
    void LoadArguments(const DexValue* args, size_t count, const char* shorty, bool is_static);

    // Value for move-result / move-result-wide / move-result-object.
    DexValue result() const { return result_; }
    void set_result(DexValue v) { result_ = v; }

    // Current instruction position, stored per frame.
    uint32_t dex_pc() const { return dex_pc_; }
    void set_dex_pc(uint32_t pc) { dex_pc_ = pc; }

    // Caught exception, read via move-exception only.
    DexObject* caught_exception() const { return caught_exception_; }
    void set_caught_exception(DexObject* ex) { caught_exception_ = ex; }

private:
    const DexMethod* method_ = nullptr;
    std::vector<DexValue> registers_;
    DexValue result_;
    uint32_t dex_pc_ = 0;
    DexObject* caught_exception_ = nullptr;
};

}  // namespace kuart
}  // namespace kudroid

#endif  // KUDROID_KUART_DEXFRAME_H
