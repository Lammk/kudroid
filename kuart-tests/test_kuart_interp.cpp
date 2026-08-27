// Host test cho Interpreter: chạy bytecode DEX thật và so kết quả.
//
// Tập trung vào các trường hợp biên mà semantics Java KHÁC C++ — chỗ dễ sai
// nhất và không tự lộ ra nếu chỉ test số bình thường:
//   INT_MIN / -1      Java trả INT_MIN, C++ là UB
//   dịch bit >= 32    Java lấy 5 bit thấp, C++ là UB
//   (int) NaN         Java trả 0, C++ là UB
//   (int) 1e30        Java saturate INT_MAX, C++ là UB
//   cmpl vs cmpg      chỉ khác nhau khi có NaN
#include "kudroid/kuart/DexClassLinker.h"
#include "kudroid/kuart/Interpreter.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>

#include "dex_builder.h"

namespace {

int g_failures = 0;

void Check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "  OK  " : "  FAIL", what.c_str());
    if (!ok) ++g_failures;
}

using namespace dexbuild;

// ── Trợ giúp sinh bytecode ───────────────────────────────────────────────
// Format 12x: op | B<<12 | A<<8
uint16_t Op12x(uint8_t op, uint8_t a, uint8_t b) {
    return static_cast<uint16_t>(op | (a << 8) | (b << 12));
}
// Format 11x / 11n
uint16_t Op11x(uint8_t op, uint8_t a) { return static_cast<uint16_t>(op | (a << 8)); }
uint16_t Op11n(uint8_t op, uint8_t a, int8_t b) {
    return static_cast<uint16_t>(op | (a << 8) | ((b & 0xF) << 12));
}
// Format 23x: op | A<<8, rồi B | C<<8
void Op23x(std::vector<uint16_t>* code, uint8_t op, uint8_t a, uint8_t b, uint8_t c) {
    code->push_back(static_cast<uint16_t>(op | (a << 8)));
    code->push_back(static_cast<uint16_t>(b | (c << 8)));
}
// Format 22b: op | A<<8, rồi B | C<<8 (C là hằng 8-bit)
void Op22b(std::vector<uint16_t>* code, uint8_t op, uint8_t a, uint8_t b, int8_t c) {
    code->push_back(static_cast<uint16_t>(op | (a << 8)));
    code->push_back(static_cast<uint16_t>((b & 0xFF) | ((c & 0xFF) << 8)));
}
// Format 21s / 21t / 21h: op | A<<8, rồi B (16-bit)
void Op21(std::vector<uint16_t>* code, uint8_t op, uint8_t a, int16_t b) {
    code->push_back(static_cast<uint16_t>(op | (a << 8)));
    code->push_back(static_cast<uint16_t>(b));
}
// Format 22t: op | B<<12 | A<<8, rồi offset
void Op22t(std::vector<uint16_t>* code, uint8_t op, uint8_t a, uint8_t b, int16_t off) {
    code->push_back(static_cast<uint16_t>(op | (a << 8) | (b << 12)));
    code->push_back(static_cast<uint16_t>(off));
}
// Format 31i: op | A<<8, rồi B (32-bit, low trước)
void Op31i(std::vector<uint16_t>* code, uint8_t op, uint8_t a, int32_t b) {
    code->push_back(static_cast<uint16_t>(op | (a << 8)));
    code->push_back(static_cast<uint16_t>(b & 0xFFFF));
    code->push_back(static_cast<uint16_t>((b >> 16) & 0xFFFF));
}
// Format 51l: op | A<<8, rồi 4 code unit
void Op51l(std::vector<uint16_t>* code, uint8_t a, int64_t v) {
    code->push_back(static_cast<uint16_t>(0x18 | (a << 8)));  // const-wide
    for (int i = 0; i < 4; ++i) {
        code->push_back(static_cast<uint16_t>((v >> (i * 16)) & 0xFFFF));
    }
}

constexpr uint8_t kOpMove = 0x01;
constexpr uint8_t kOpMoveWide = 0x06;
constexpr uint8_t kOpConst4 = 0x12;
constexpr uint8_t kOpConst16 = 0x13;
constexpr uint8_t kOpConst = 0x14;
constexpr uint8_t kOpConstWide16 = 0x16;
constexpr uint8_t kOpReturnVoid = 0x0e;
constexpr uint8_t kOpReturn = 0x0f;
constexpr uint8_t kOpReturnWide = 0x10;
constexpr uint8_t kOpGoto = 0x28;
constexpr uint8_t kOpIfGe = 0x35;
constexpr uint8_t kOpCmplFloat = 0x2d;
constexpr uint8_t kOpCmpgFloat = 0x2e;
constexpr uint8_t kOpCmpLong = 0x31;
constexpr uint8_t kOpIntToFloat = 0x82;
constexpr uint8_t kOpIntToDouble = 0x83;
constexpr uint8_t kOpLongToInt = 0x84;
constexpr uint8_t kOpFloatToInt = 0x87;
constexpr uint8_t kOpDoubleToInt = 0x8a;
constexpr uint8_t kOpIntToByte = 0x8d;
constexpr uint8_t kOpIntToChar = 0x8e;
constexpr uint8_t kOpAddInt = 0x90;
constexpr uint8_t kOpSubInt = 0x91;
constexpr uint8_t kOpMulInt = 0x92;
constexpr uint8_t kOpDivInt = 0x93;
constexpr uint8_t kOpRemInt = 0x94;
constexpr uint8_t kOpShlInt = 0x98;
constexpr uint8_t kOpUshrInt = 0x9a;
constexpr uint8_t kOpAddLong = 0x9b;
constexpr uint8_t kOpMulLong = 0x9d;
constexpr uint8_t kOpDivLong = 0x9e;
constexpr uint8_t kOpAddFloat = 0xa6;
constexpr uint8_t kOpDivFloat = 0xa9;
constexpr uint8_t kOpAddDouble = 0xab;
constexpr uint8_t kOpAddInt2Addr = 0xb0;
constexpr uint8_t kOpAddIntLit8 = 0xd8;
constexpr uint8_t kOpMulIntLit8 = 0xda;

// Mỗi method test nhận tham số ở register cuối và trả kết quả.
MethodSpec MakeMethod(const std::string& name, const std::string& ret,
                      const std::vector<std::string>& params,
                      const std::vector<uint16_t>& code, uint16_t registers,
                      uint16_t ins) {
    MethodSpec m;
    m.name = name;
    m.return_type = ret;
    m.params = params;
    m.access_flags = 0x9;  // ACC_PUBLIC | ACC_STATIC
    m.code = code;
    m.registers_size = registers;
    m.ins_size = ins;
    return m;
}

std::vector<ClassSpec> MakeTestClasses() {
    ClassSpec object;
    object.descriptor = "Ljava/lang/Object;";
    object.superclass = "";

    ClassSpec t;
    t.descriptor = "LT;";
    t.superclass = "Ljava/lang/Object;";

    // int addInt(int a, int b) { return a + b; }  regs: v0 kq, v1=a, v2=b
    {
        std::vector<uint16_t> code;
        Op23x(&code, kOpAddInt, 0, 1, 2);
        code.push_back(Op11x(kOpReturn, 0));
        t.direct_methods.push_back(MakeMethod("addInt", "I", {"I", "I"}, code, 3, 2));
    }
    // int divInt(int a, int b) { return a / b; }
    {
        std::vector<uint16_t> code;
        Op23x(&code, kOpDivInt, 0, 1, 2);
        code.push_back(Op11x(kOpReturn, 0));
        t.direct_methods.push_back(MakeMethod("divInt", "I", {"I", "I"}, code, 3, 2));
    }
    // int remInt(int a, int b) { return a % b; }
    {
        std::vector<uint16_t> code;
        Op23x(&code, kOpRemInt, 0, 1, 2);
        code.push_back(Op11x(kOpReturn, 0));
        t.direct_methods.push_back(MakeMethod("remInt", "I", {"I", "I"}, code, 3, 2));
    }
    // int shlInt(int a, int b) { return a << b; }
    {
        std::vector<uint16_t> code;
        Op23x(&code, kOpShlInt, 0, 1, 2);
        code.push_back(Op11x(kOpReturn, 0));
        t.direct_methods.push_back(MakeMethod("shlInt", "I", {"I", "I"}, code, 3, 2));
    }
    // int ushrInt(int a, int b) { return a >>> b; }
    {
        std::vector<uint16_t> code;
        Op23x(&code, kOpUshrInt, 0, 1, 2);
        code.push_back(Op11x(kOpReturn, 0));
        t.direct_methods.push_back(MakeMethod("ushrInt", "I", {"I", "I"}, code, 3, 2));
    }
    // long mulLong(long a, long b) { return a * b; }  v0/v1 kq, v2=a, v4=b
    {
        std::vector<uint16_t> code;
        Op23x(&code, kOpMulLong, 0, 2, 4);
        code.push_back(Op11x(kOpReturnWide, 0));
        t.direct_methods.push_back(MakeMethod("mulLong", "J", {"J", "J"}, code, 6, 4));
    }
    // long divLong(long a, long b)
    {
        std::vector<uint16_t> code;
        Op23x(&code, kOpDivLong, 0, 2, 4);
        code.push_back(Op11x(kOpReturnWide, 0));
        t.direct_methods.push_back(MakeMethod("divLong", "J", {"J", "J"}, code, 6, 4));
    }
    // float divFloat(float a, float b)
    {
        std::vector<uint16_t> code;
        Op23x(&code, kOpDivFloat, 0, 1, 2);
        code.push_back(Op11x(kOpReturn, 0));
        t.direct_methods.push_back(MakeMethod("divFloat", "F", {"F", "F"}, code, 3, 2));
    }
    // double addDouble(double a, double b)  v0/v1 kq, v2=a, v4=b
    {
        std::vector<uint16_t> code;
        Op23x(&code, kOpAddDouble, 0, 2, 4);
        code.push_back(Op11x(kOpReturnWide, 0));
        t.direct_methods.push_back(MakeMethod("addDouble", "D", {"D", "D"}, code, 6, 4));
    }
    // int floatToInt(float a) { return (int) a; }
    {
        std::vector<uint16_t> code;
        code.push_back(Op12x(kOpFloatToInt, 0, 1));
        code.push_back(Op11x(kOpReturn, 0));
        t.direct_methods.push_back(MakeMethod("floatToInt", "I", {"F"}, code, 2, 1));
    }
    // int doubleToInt(double a)  v0 kq, v1/v2 = a
    {
        std::vector<uint16_t> code;
        code.push_back(Op12x(kOpDoubleToInt, 0, 1));
        code.push_back(Op11x(kOpReturn, 0));
        t.direct_methods.push_back(MakeMethod("doubleToInt", "I", {"D"}, code, 3, 2));
    }
    // int cmplFloat(float a, float b)
    {
        std::vector<uint16_t> code;
        Op23x(&code, kOpCmplFloat, 0, 1, 2);
        code.push_back(Op11x(kOpReturn, 0));
        t.direct_methods.push_back(MakeMethod("cmplFloat", "I", {"F", "F"}, code, 3, 2));
    }
    // int cmpgFloat(float a, float b)
    {
        std::vector<uint16_t> code;
        Op23x(&code, kOpCmpgFloat, 0, 1, 2);
        code.push_back(Op11x(kOpReturn, 0));
        t.direct_methods.push_back(MakeMethod("cmpgFloat", "I", {"F", "F"}, code, 3, 2));
    }
    // int cmpLong(long a, long b)  v0 kq, v1=a, v3=b
    {
        std::vector<uint16_t> code;
        Op23x(&code, kOpCmpLong, 0, 1, 3);
        code.push_back(Op11x(kOpReturn, 0));
        t.direct_methods.push_back(MakeMethod("cmpLong", "I", {"J", "J"}, code, 5, 4));
    }
    // int intToByte(int a) { return (byte) a; }
    {
        std::vector<uint16_t> code;
        code.push_back(Op12x(kOpIntToByte, 0, 1));
        code.push_back(Op11x(kOpReturn, 0));
        t.direct_methods.push_back(MakeMethod("intToByte", "I", {"I"}, code, 2, 1));
    }
    // int intToChar(int a) { return (char) a; }
    {
        std::vector<uint16_t> code;
        code.push_back(Op12x(kOpIntToChar, 0, 1));
        code.push_back(Op11x(kOpReturn, 0));
        t.direct_methods.push_back(MakeMethod("intToChar", "I", {"I"}, code, 2, 1));
    }
    // int addLit8(int a) { return a + 100; }
    {
        std::vector<uint16_t> code;
        Op22b(&code, kOpAddIntLit8, 0, 1, 100);
        code.push_back(Op11x(kOpReturn, 0));
        t.direct_methods.push_back(MakeMethod("addLit8", "I", {"I"}, code, 2, 1));
    }
    // int sumLoop(int n) { int s = 0; for (int i = 0; i < n; i++) s += i; return s; }
    // v0 = s, v1 = i, v2 = n (tham số)
    {
        std::vector<uint16_t> code;
        code.push_back(Op11n(kOpConst4, 0, 0));   // s = 0
        code.push_back(Op11n(kOpConst4, 1, 0));   // i = 0
        // loop: if (i >= n) goto end
        Op22t(&code, kOpIfGe, 1, 2, 6);           // +6 code unit tới end
        code.push_back(Op12x(kOpAddInt2Addr, 0, 1));  // s += i
        Op22b(&code, kOpAddIntLit8, 1, 1, 1);     // i = i + 1
        // goto về pc 2 (if-ge). Đang ở pc 7 → offset -5. Dùng -4 sẽ nhảy vào
        // giữa instruction if-ge (2 code unit) → lặp vô hạn.
        code.push_back(static_cast<uint16_t>(kOpGoto | ((-5 & 0xFF) << 8)));
        code.push_back(Op11x(kOpReturn, 0));      // end: return s
        t.direct_methods.push_back(MakeMethod("sumLoop", "I", {"I"}, code, 3, 1));
    }
    // int constants() — kiểm tra const/const-16/const
    {
        std::vector<uint16_t> code;
        Op31i(&code, kOpConst, 0, 0x12345678);
        code.push_back(Op11x(kOpReturn, 0));
        t.direct_methods.push_back(MakeMethod("bigConst", "I", {}, code, 1, 0));
    }
    // long constWide() { return 0x1122334455667788L; }
    {
        std::vector<uint16_t> code;
        Op51l(&code, 0, 0x1122334455667788LL);
        code.push_back(Op11x(kOpReturnWide, 0));
        t.direct_methods.push_back(MakeMethod("constWide", "J", {}, code, 2, 0));
    }
    // void nothing() { return; }
    {
        std::vector<uint16_t> code;
        code.push_back(Op11x(kOpReturnVoid, 0));
        t.direct_methods.push_back(MakeMethod("nothing", "V", {}, code, 1, 0));
    }

    return {object, t};
}

using kudroid::kuart::DexValue;

}  // namespace

int main() {
    std::printf("=== KuART Interpreter: chạy bytecode DEX thật ===\n");

    DexBuilder builder;
    const std::vector<uint8_t> dex = builder.Build(MakeTestClasses());
    std::printf("DEX synthetic: %zu bytes\n", dex.size());

    kudroid::kuart::DexClassLinker linker;
    std::string error;
    if (!linker.AddDexFile(dex.data(), dex.size(), "test.dex", &error)) {
        std::printf("  FAIL AddDexFile: %s\n=== FAILED ===\n", error.c_str());
        return 1;
    }

    kudroid::kuart::DexClass* klass = linker.FindClass("LT;");
    if (klass == nullptr) {
        std::printf("  FAIL FindClass(LT;): %s\n=== FAILED ===\n",
                    linker.last_error().c_str());
        return 1;
    }

    kudroid::kuart::Interpreter interp(&linker);

    // Gọi method static, trả về DexValue.
    const auto call = [&](const char* name, const char* sig,
                          std::vector<DexValue> args) -> DexValue {
        interp.ClearPendingException();
        kudroid::kuart::DexMethod* m = klass->FindDirectMethod(name, sig);
        if (m == nullptr) {
            std::printf("  FAIL không tìm thấy method %s%s\n", name, sig);
            ++g_failures;
            return DexValue();
        }
        return interp.Execute(m, args.data(), args.size());
    };

    // ── số học cơ bản ──
    Check(call("addInt", "(II)I", {DexValue::Int(3), DexValue::Int(4)}).i == 7,
          "addInt(3,4) == 7");
    Check(call("addInt", "(II)I", {DexValue::Int(-10), DexValue::Int(4)}).i == -6,
          "addInt(-10,4) == -6");

    // Cộng tràn phải wrap, không UB.
    Check(call("addInt", "(II)I",
               {DexValue::Int(std::numeric_limits<int32_t>::max()), DexValue::Int(1)}).i ==
              std::numeric_limits<int32_t>::min(),
          "addInt(INT_MAX,1) wrap về INT_MIN");

    Check(call("divInt", "(II)I", {DexValue::Int(20), DexValue::Int(3)}).i == 6,
          "divInt(20,3) == 6");
    // Java làm tròn về 0, không phải về âm vô cực.
    Check(call("divInt", "(II)I", {DexValue::Int(-20), DexValue::Int(3)}).i == -6,
          "divInt(-20,3) == -6 (làm tròn về 0)");
    Check(call("remInt", "(II)I", {DexValue::Int(-20), DexValue::Int(3)}).i == -2,
          "remInt(-20,3) == -2 (dấu theo số bị chia)");

    // INT_MIN / -1: Java trả INT_MIN, C++ thuần là UB.
    Check(call("divInt", "(II)I",
               {DexValue::Int(std::numeric_limits<int32_t>::min()), DexValue::Int(-1)}).i ==
              std::numeric_limits<int32_t>::min(),
          "divInt(INT_MIN,-1) == INT_MIN (không UB)");
    Check(call("remInt", "(II)I",
               {DexValue::Int(std::numeric_limits<int32_t>::min()), DexValue::Int(-1)}).i == 0,
          "remInt(INT_MIN,-1) == 0");

    // ── chia cho 0 phải ném exception, không crash ──
    {
        interp.ClearPendingException();
        kudroid::kuart::DexMethod* m = klass->FindDirectMethod("divInt", "(II)I");
        std::vector<DexValue> args = {DexValue::Int(1), DexValue::Int(0)};
        interp.Execute(m, args.data(), args.size());
        Check(interp.HasPendingException(), "divInt(1,0) ném exception");
        Check(interp.last_error().find("ArithmeticException") != std::string::npos,
              "exception là ArithmeticException");
        interp.ClearPendingException();
    }

    // ── dịch bit: Java chỉ dùng 5 bit thấp ──
    Check(call("shlInt", "(II)I", {DexValue::Int(1), DexValue::Int(4)}).i == 16,
          "shlInt(1,4) == 16");
    Check(call("shlInt", "(II)I", {DexValue::Int(1), DexValue::Int(32)}).i == 1,
          "shlInt(1,32) == 1 (dịch 32 = dịch 0)");
    Check(call("shlInt", "(II)I", {DexValue::Int(1), DexValue::Int(33)}).i == 2,
          "shlInt(1,33) == 2 (chỉ lấy 5 bit thấp)");
    Check(call("ushrInt", "(II)I", {DexValue::Int(-1), DexValue::Int(28)}).i == 0xF,
          "ushrInt(-1,28) == 0xF (dịch logic, không giữ dấu)");

    // ── long ──
    Check(call("mulLong", "(JJ)J",
               {DexValue::Long(1000000LL), DexValue::Long(1000000LL)}).j == 1000000000000LL,
          "mulLong(1e6,1e6) == 1e12 (không tràn 32-bit)");
    Check(call("divLong", "(JJ)J",
               {DexValue::Long(std::numeric_limits<int64_t>::min()), DexValue::Long(-1)}).j ==
              std::numeric_limits<int64_t>::min(),
          "divLong(LONG_MIN,-1) == LONG_MIN");
    Check(call("cmpLong", "(JJ)I", {DexValue::Long(5), DexValue::Long(9)}).i == -1,
          "cmpLong(5,9) == -1");
    Check(call("cmpLong", "(JJ)I", {DexValue::Long(9), DexValue::Long(5)}).i == 1,
          "cmpLong(9,5) == 1");
    Check(call("cmpLong", "(JJ)I", {DexValue::Long(7), DexValue::Long(7)}).i == 0,
          "cmpLong(7,7) == 0");

    // ── float/double ──
    Check(call("divFloat", "(FF)F", {DexValue::Float(7.0f), DexValue::Float(2.0f)}).f == 3.5f,
          "divFloat(7,2) == 3.5");
    // Chia 0 trong float KHÔNG ném exception (khác int) — trả về Infinity.
    Check(std::isinf(call("divFloat", "(FF)F",
                          {DexValue::Float(1.0f), DexValue::Float(0.0f)}).f),
          "divFloat(1,0) == Infinity (không ném exception)");
    Check(call("addDouble", "(DD)D", {DexValue::Double(0.5), DexValue::Double(0.25)}).d == 0.75,
          "addDouble(0.5,0.25) == 0.75");

    // ── đổi kiểu: saturate + NaN ──
    Check(call("floatToInt", "(F)I", {DexValue::Float(3.9f)}).i == 3,
          "floatToInt(3.9) == 3 (cắt về 0)");
    Check(call("floatToInt", "(F)I", {DexValue::Float(-3.9f)}).i == -3,
          "floatToInt(-3.9) == -3");
    Check(call("floatToInt", "(F)I",
               {DexValue::Float(std::numeric_limits<float>::quiet_NaN())}).i == 0,
          "floatToInt(NaN) == 0");
    Check(call("floatToInt", "(F)I", {DexValue::Float(1e30f)}).i ==
              std::numeric_limits<int32_t>::max(),
          "floatToInt(1e30) saturate INT_MAX");
    Check(call("floatToInt", "(F)I", {DexValue::Float(-1e30f)}).i ==
              std::numeric_limits<int32_t>::min(),
          "floatToInt(-1e30) saturate INT_MIN");
    Check(call("doubleToInt", "(D)I", {DexValue::Double(1e300)}).i ==
              std::numeric_limits<int32_t>::max(),
          "doubleToInt(1e300) saturate INT_MAX");

    // ── cmpl vs cmpg: chỉ khác nhau ở NaN ──
    const float nan = std::numeric_limits<float>::quiet_NaN();
    Check(call("cmplFloat", "(FF)I", {DexValue::Float(nan), DexValue::Float(1.0f)}).i == -1,
          "cmplFloat(NaN,1) == -1");
    Check(call("cmpgFloat", "(FF)I", {DexValue::Float(nan), DexValue::Float(1.0f)}).i == 1,
          "cmpgFloat(NaN,1) == 1");
    Check(call("cmplFloat", "(FF)I", {DexValue::Float(1.0f), DexValue::Float(2.0f)}).i == -1,
          "cmplFloat(1,2) == -1 (không NaN thì giống nhau)");
    Check(call("cmpgFloat", "(FF)I", {DexValue::Float(1.0f), DexValue::Float(2.0f)}).i == -1,
          "cmpgFloat(1,2) == -1");

    // ── thu hẹp kiểu ──
    Check(call("intToByte", "(I)I", {DexValue::Int(0x1FF)}).i == -1,
          "intToByte(0x1FF) == -1 (mở rộng dấu)");
    Check(call("intToChar", "(I)I", {DexValue::Int(-1)}).i == 0xFFFF,
          "intToChar(-1) == 0xFFFF (không dấu)");

    // ── hằng ──
    Check(call("addLit8", "(I)I", {DexValue::Int(5)}).i == 105, "addLit8(5) == 105");
    Check(call("bigConst", "()I", {}).i == 0x12345678, "bigConst() == 0x12345678");
    Check(call("constWide", "()J", {}).j == 0x1122334455667788LL,
          "constWide() == 0x1122334455667788");

    // ── vòng lặp: goto + if + 2addr ──
    Check(call("sumLoop", "(I)I", {DexValue::Int(5)}).i == 10, "sumLoop(5) == 10 (0+1+2+3+4)");
    Check(call("sumLoop", "(I)I", {DexValue::Int(100)}).i == 4950, "sumLoop(100) == 4950");
    Check(call("sumLoop", "(I)I", {DexValue::Int(0)}).i == 0, "sumLoop(0) == 0 (không vào vòng)");

    // ── return void ──
    {
        interp.ClearPendingException();
        kudroid::kuart::DexMethod* m = klass->FindDirectMethod("nothing", "()V");
        interp.Execute(m, nullptr, 0);
        Check(!interp.HasPendingException(), "nothing() chạy xong không exception");
    }

    std::printf("đã chạy %llu instruction\n",
                static_cast<unsigned long long>(interp.instructions_executed()));

    if (g_failures == 0) {
        std::printf("=== KuART Interpreter test PASSED ===\n");
        return 0;
    }
    std::printf("=== KuART Interpreter test FAILED (%d lỗi) ===\n", g_failures);
    return 1;
}
