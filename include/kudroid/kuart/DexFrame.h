// Khung gọi của interpreter: mảng register + vị trí bytecode đang chạy.
//
// Register trong DEX là 32-bit và long/double chiếm một CẶP (vN, vN+1). Ở đây
// mỗi slot là DexValue 64-bit nên giá trị wide nằm gọn trong slot N, slot N+1
// bỏ trống. Bytecode hợp lệ không bao giờ đọc riêng nửa sau của một cặp wide
// (verifier của Android chặn từ lúc build APK), nên cách này an toàn và bỏ
// được toàn bộ việc ghép/tách 32-bit như ART phải làm.
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

    // Tham số nằm ở các register CUỐI cùng của frame (quy ước DEX), gồm cả
    // `this` cho method instance.
    uint32_t FirstArgRegister() const {
        if (method_ == nullptr) return 0;
        const uint32_t regs = method_->registers_size;
        const uint32_t ins = method_->ins_size;
        return regs >= ins ? regs - ins : 0;
    }

    // Nạp tham số vào các register cuối. `args` theo thứ tự khai báo, long/
    // double tính MỘT phần tử (đã gộp trong DexValue) nhưng chiếm HAI slot.
    void LoadArguments(const DexValue* args, size_t count, const char* shorty, bool is_static);

    // Giá trị của move-result / move-result-wide / move-result-object.
    DexValue result() const { return result_; }
    void set_result(DexValue v) { result_ = v; }

    // Vị trí instruction đang chạy. Lưu theo frame (không theo Interpreter) vì
    // method này có thể đang gọi method khác — mỗi frame có pc riêng.
    uint32_t dex_pc() const { return dex_pc_; }
    void set_dex_pc(uint32_t pc) { dex_pc_ = pc; }

    // Exception đã bắt được, chờ move-exception đọc ra.
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
