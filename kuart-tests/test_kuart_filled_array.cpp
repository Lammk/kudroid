// Host test for filled-new-array / filled-new-array/range.
//
// Why this opcode gets its own test file: d8 emits it for every `new T[]{...}`
// literal, and that includes the synthetic `$values()` method it generates for
// EVERY enum. Decoding the shipped framework.dex shows 13 uses, among them
// ActivityThread.handleLaunchActivity, Activity.setContentView and six
// enum $values() methods. Before this opcode existed the interpreter hit its
// default: case and threw UnsupportedOperationException, so no enum could run
// its <clinit> — which is exactly the failure seen when launching a real APK.
//
// Two properties are easy to get wrong and are asserted explicitly here:
//   1. VRegB is the ARRAY type ("[I"), not the element type. Resolving it and
//      slicing the descriptor instead of reading component_type breaks "[[I".
//   2. The result is read back with move-result-object (like an invoke), NOT
//      written into a destination register.
#include "kudroid/kuart/DexClassLinker.h"
#include "kudroid/kuart/Interpreter.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "dex_builder.h"

namespace {

int g_failures = 0;

void Check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "  OK  " : "  FAIL", what.c_str());
    if (!ok) ++g_failures;
}

using namespace dexbuild;
using kudroid::kuart::DexArray;
using kudroid::kuart::DexObject;
using kudroid::kuart::DexValue;

uint16_t Op11x(uint8_t op, uint8_t a) { return static_cast<uint16_t>(op | (a << 8)); }
uint16_t Op11n(uint8_t op, uint8_t a, int8_t b) {
    return static_cast<uint16_t>(op | (a << 8) | ((b & 0xF) << 12));
}
uint16_t Op12x(uint8_t op, uint8_t a, uint8_t b) {
    return static_cast<uint16_t>(op | (a << 8) | (b << 12));
}
void Op21c(std::vector<uint16_t>* code, uint8_t op, uint8_t a, uint16_t idx) {
    code->push_back(static_cast<uint16_t>(op | (a << 8)));
    code->push_back(idx);
}
void Op22c(std::vector<uint16_t>* code, uint8_t op, uint8_t a, uint8_t b, uint16_t idx) {
    code->push_back(static_cast<uint16_t>(op | (a << 8) | (b << 12)));
    code->push_back(idx);
}
void Op23x(std::vector<uint16_t>* code, uint8_t op, uint8_t a, uint8_t b, uint8_t c) {
    code->push_back(static_cast<uint16_t>(op | (a << 8)));
    code->push_back(static_cast<uint16_t>(b | (c << 8)));
}
void Op51l(std::vector<uint16_t>* code, uint8_t a, int64_t v) {
    code->push_back(static_cast<uint16_t>(0x18 | (a << 8)));  // const-wide
    for (int i = 0; i < 4; ++i) {
        code->push_back(static_cast<uint16_t>((v >> (i * 16)) & 0xFFFF));
    }
}

// filled-new-array (k35c): op | A<<12, type@BBBB, then packed 4-bit registers.
// A is the element count (max 5: kMaxVarArgRegs). The 5th register lives in the
// high nibble of the first code unit, exactly like invoke's 35c encoding.
void OpFilledNewArray(std::vector<uint16_t>* code, uint16_t type_idx,
                      const std::vector<uint8_t>& regs) {
    uint16_t first = static_cast<uint16_t>(0x24 | (regs.size() << 12));
    if (regs.size() == 5) {
        first = static_cast<uint16_t>(first | ((regs[4] & 0xF) << 8));
    }
    code->push_back(first);
    code->push_back(type_idx);
    uint16_t packed = 0;
    for (size_t i = 0; i < regs.size() && i < 4; ++i) {
        packed |= static_cast<uint16_t>((regs[i] & 0xF) << (i * 4));
    }
    code->push_back(packed);
}

// filled-new-array/range (k3rc): op | AA<<8, type@BBBB, first register CCCC.
void OpFilledNewArrayRange(std::vector<uint16_t>* code, uint16_t type_idx, uint8_t count,
                           uint16_t first_reg) {
    code->push_back(static_cast<uint16_t>(0x25 | (count << 8)));
    code->push_back(type_idx);
    code->push_back(first_reg);
}

constexpr uint8_t kOpConst4 = 0x12;
constexpr uint8_t kOpConst16 = 0x13;
constexpr uint8_t kOpReturn = 0x0f;
constexpr uint8_t kOpReturnWide = 0x10;
constexpr uint8_t kOpReturnObject = 0x11;
constexpr uint8_t kOpReturnVoid = 0x0e;
constexpr uint8_t kOpMoveResultObject = 0x0c;
constexpr uint8_t kOpArrayLength = 0x21;
constexpr uint8_t kOpAget = 0x44;
constexpr uint8_t kOpAgetWide = 0x45;
constexpr uint8_t kOpAgetObject = 0x46;
constexpr uint8_t kOpAgetByte = 0x48;
constexpr uint8_t kOpAgetChar = 0x49;
constexpr uint8_t kOpAgetShort = 0x4A;
constexpr uint8_t kOpConstString = 0x1a;
constexpr uint8_t kOpAddInt = 0x90;

struct Specs {
    MethodSpec object_ctor;

    MethodSpec int_array;      // new int[]{7, 9, 11}       -> returns [I
    MethodSpec int_sum;        // sums that array           -> returns I
    MethodSpec five_elems;     // new int[]{1,2,3,4,5}      -> max k35c width
    MethodSpec empty_array;    // new int[]{}               -> zero elements
    MethodSpec byte_array;     // new byte[]{-1, 2}         -> narrow element width
    MethodSpec char_array;     // new char[]{0xFFFF}        -> unsigned narrow
    MethodSpec short_array;    // new short[]{-2}           -> signed narrow
    MethodSpec long_array;     // new long[]{big} via range -> wide element
    MethodSpec string_array;   // new String[]{"a","b"}     -> reference element
    MethodSpec range_array;    // filled-new-array/range with 6 ints
    MethodSpec nested_array;   // new int[][]{a, b}         -> "[[I" component

    Specs() {
        object_ctor.name = "<init>";
        object_ctor.access_flags = 0x10001;

        int_array.name = "intArray";
        int_array.return_type = "[I";
        int_array.access_flags = 0x9;

        int_sum.name = "intSum";
        int_sum.return_type = "I";
        int_sum.access_flags = 0x9;

        five_elems.name = "fiveElems";
        five_elems.return_type = "I";
        five_elems.access_flags = 0x9;

        empty_array.name = "emptyArray";
        empty_array.return_type = "I";
        empty_array.access_flags = 0x9;

        byte_array.name = "byteArray";
        byte_array.return_type = "I";
        byte_array.access_flags = 0x9;

        char_array.name = "charArray";
        char_array.return_type = "I";
        char_array.access_flags = 0x9;

        short_array.name = "shortArray";
        short_array.return_type = "I";
        short_array.access_flags = 0x9;

        long_array.name = "longArray";
        long_array.return_type = "J";
        long_array.access_flags = 0x9;

        string_array.name = "stringArray";
        string_array.return_type = "Ljava/lang/String;";
        string_array.access_flags = 0x9;

        range_array.name = "rangeArray";
        range_array.return_type = "I";
        range_array.access_flags = 0x9;

        nested_array.name = "nestedArray";
        nested_array.return_type = "I";
        nested_array.access_flags = 0x9;
    }
};

std::vector<ClassSpec> BuildClasses(const Specs& s) {
    ClassSpec object;
    object.descriptor = "Ljava/lang/Object;";
    object.superclass = "";
    object.direct_methods = {s.object_ctor};

    ClassSpec string;
    string.descriptor = "Ljava/lang/String;";
    string.superclass = "Ljava/lang/Object;";

    ClassSpec t;
    t.descriptor = "LT;";
    t.superclass = "Ljava/lang/Object;";
    // These only appear inside bytecode operands, so the builder cannot discover
    // them from the field/method tables — they must be declared explicitly.
    t.extra_types = {"[I", "[B", "[C", "[S", "[J", "[Ljava/lang/String;", "[[I"};
    t.extra_strings = {"alpha", "beta"};
    t.direct_methods = {s.int_array,   s.int_sum,      s.five_elems,   s.empty_array,
                        s.byte_array,  s.char_array,   s.short_array,  s.long_array,
                        s.string_array, s.range_array, s.nested_array};

    return {object, string, t};
}

}  // namespace

int main() {
    std::printf("=== KuART filled-new-array / filled-new-array/range ===\n");

    // Pass 1: learn the table indices (they only settle after the builder sorts).
    Specs probe;
    DexBuilder index_builder;
    index_builder.Build(BuildClasses(probe));

    const uint16_t kIntArr = static_cast<uint16_t>(index_builder.TypeIndexOf("[I"));
    const uint16_t kByteArr = static_cast<uint16_t>(index_builder.TypeIndexOf("[B"));
    const uint16_t kCharArr = static_cast<uint16_t>(index_builder.TypeIndexOf("[C"));
    const uint16_t kShortArr = static_cast<uint16_t>(index_builder.TypeIndexOf("[S"));
    const uint16_t kLongArr = static_cast<uint16_t>(index_builder.TypeIndexOf("[J"));
    const uint16_t kStrArr =
        static_cast<uint16_t>(index_builder.TypeIndexOf("[Ljava/lang/String;"));
    const uint16_t kNestedArr = static_cast<uint16_t>(index_builder.TypeIndexOf("[[I"));
    const uint16_t kStrAlpha = static_cast<uint16_t>(index_builder.StringIndexOf("alpha"));
    const uint16_t kStrBeta = static_cast<uint16_t>(index_builder.StringIndexOf("beta"));

    // Pass 2: real bytecode.
    Specs s;
    s.object_ctor.code = {Op11x(kOpReturnVoid, 0)};
    s.object_ctor.registers_size = 1;
    s.object_ctor.ins_size = 1;

    // int[] intArray() { return new int[]{7, 9, 11}; }
    {
        std::vector<uint16_t> c;
        Op21c(&c, kOpConst16, 1, 7);
        Op21c(&c, kOpConst16, 2, 9);
        Op21c(&c, kOpConst16, 3, 11);
        OpFilledNewArray(&c, kIntArr, {1, 2, 3});
        c.push_back(Op11x(kOpMoveResultObject, 0));
        c.push_back(Op11x(kOpReturnObject, 0));
        s.int_array.code = c;
        s.int_array.registers_size = 4;
    }
    // int intSum() { int[] a = new int[]{7, 9, 11}; return a[0]+a[1]+a[2]; }
    {
        std::vector<uint16_t> c;
        Op21c(&c, kOpConst16, 1, 7);
        Op21c(&c, kOpConst16, 2, 9);
        Op21c(&c, kOpConst16, 3, 11);
        OpFilledNewArray(&c, kIntArr, {1, 2, 3});
        c.push_back(Op11x(kOpMoveResultObject, 0));
        c.push_back(Op11n(kOpConst4, 1, 0));
        Op23x(&c, kOpAget, 4, 0, 1);   // v4 = a[0]
        c.push_back(Op11n(kOpConst4, 1, 1));
        Op23x(&c, kOpAget, 5, 0, 1);   // v5 = a[1]
        Op23x(&c, kOpAddInt, 4, 4, 5);
        c.push_back(Op11n(kOpConst4, 1, 2));
        Op23x(&c, kOpAget, 5, 0, 1);   // v5 = a[2]
        Op23x(&c, kOpAddInt, 4, 4, 5);
        c.push_back(Op11x(kOpReturn, 4));
        s.int_sum.code = c;
        s.int_sum.registers_size = 6;
    }
    // int fiveElems() { int[] a = new int[]{1,2,3,4,5}; return a.length*100 + a[4]; }
    // Exercises the 5-register k35c form where the 5th register is encoded in
    // the high nibble of the first code unit.
    {
        std::vector<uint16_t> c;
        c.push_back(Op11n(kOpConst4, 1, 1));
        c.push_back(Op11n(kOpConst4, 2, 2));
        c.push_back(Op11n(kOpConst4, 3, 3));
        c.push_back(Op11n(kOpConst4, 4, 4));
        c.push_back(Op11n(kOpConst4, 5, 5));
        OpFilledNewArray(&c, kIntArr, {1, 2, 3, 4, 5});
        c.push_back(Op11x(kOpMoveResultObject, 0));
        c.push_back(Op11n(kOpConst4, 1, 4));
        Op23x(&c, kOpAget, 2, 0, 1);      // v2 = a[4] = 5
        c.push_back(Op12x(kOpArrayLength, 3, 0));
        Op23x(&c, kOpAddInt, 2, 2, 3);    // 5 + 5 = 10
        c.push_back(Op11x(kOpReturn, 2));
        s.five_elems.code = c;
        s.five_elems.registers_size = 6;
    }
    // int emptyArray() { return new int[]{}.length; }
    {
        std::vector<uint16_t> c;
        OpFilledNewArray(&c, kIntArr, {});
        c.push_back(Op11x(kOpMoveResultObject, 0));
        c.push_back(Op12x(kOpArrayLength, 1, 0));
        c.push_back(Op11x(kOpReturn, 1));
        s.empty_array.code = c;
        s.empty_array.registers_size = 2;
    }
    // int byteArray() { byte[] a = new byte[]{-1, 2}; return a[0]; }  -> -1 sign extended
    {
        std::vector<uint16_t> c;
        c.push_back(Op11n(kOpConst4, 1, -1));
        c.push_back(Op11n(kOpConst4, 2, 2));
        OpFilledNewArray(&c, kByteArr, {1, 2});
        c.push_back(Op11x(kOpMoveResultObject, 0));
        c.push_back(Op11n(kOpConst4, 1, 0));
        Op23x(&c, kOpAgetByte, 2, 0, 1);
        c.push_back(Op11x(kOpReturn, 2));
        s.byte_array.code = c;
        s.byte_array.registers_size = 3;
    }
    // int charArray() { char[] a = new char[]{(char)-1}; return a[0]; } -> 0xFFFF unsigned
    {
        std::vector<uint16_t> c;
        c.push_back(Op11n(kOpConst4, 1, -1));
        OpFilledNewArray(&c, kCharArr, {1});
        c.push_back(Op11x(kOpMoveResultObject, 0));
        c.push_back(Op11n(kOpConst4, 1, 0));
        Op23x(&c, kOpAgetChar, 2, 0, 1);
        c.push_back(Op11x(kOpReturn, 2));
        s.char_array.code = c;
        s.char_array.registers_size = 3;
    }
    // int shortArray() { short[] a = new short[]{-2}; return a[0]; } -> -2 sign extended
    {
        std::vector<uint16_t> c;
        c.push_back(Op11n(kOpConst4, 1, -2));
        OpFilledNewArray(&c, kShortArr, {1});
        c.push_back(Op11x(kOpMoveResultObject, 0));
        c.push_back(Op11n(kOpConst4, 1, 0));
        Op23x(&c, kOpAgetShort, 2, 0, 1);
        c.push_back(Op11x(kOpReturn, 2));
        s.short_array.code = c;
        s.short_array.registers_size = 3;
    }
    // long longArray() { long[] a = new long[]{0x1122334455667788L}; return a[0]; }
    // Wide element: in KuART one vreg holds the whole 64-bit value, so the
    // element writer must store 8 bytes from a single register.
    {
        std::vector<uint16_t> c;
        Op51l(&c, 2, 0x1122334455667788LL);
        OpFilledNewArrayRange(&c, kLongArr, 1, 2);
        c.push_back(Op11x(kOpMoveResultObject, 0));
        c.push_back(Op11n(kOpConst4, 1, 0));
        Op23x(&c, kOpAgetWide, 4, 0, 1);
        c.push_back(Op11x(kOpReturnWide, 4));
        s.long_array.code = c;
        s.long_array.registers_size = 6;
    }
    // String stringArray() { String[] a = new String[]{"alpha","beta"}; return a[1]; }
    {
        std::vector<uint16_t> c;
        Op21c(&c, kOpConstString, 1, kStrAlpha);
        Op21c(&c, kOpConstString, 2, kStrBeta);
        OpFilledNewArray(&c, kStrArr, {1, 2});
        c.push_back(Op11x(kOpMoveResultObject, 0));
        c.push_back(Op11n(kOpConst4, 1, 1));
        Op23x(&c, kOpAgetObject, 2, 0, 1);
        c.push_back(Op11x(kOpReturnObject, 2));
        s.string_array.code = c;
        s.string_array.registers_size = 3;
    }
    // int rangeArray() { int[] a = new int[]{10,20,30,40,50,60}; return a.length*1000 + a[5]; }
    // Six elements is past k35c's 5-register limit, so d8 would emit /range here.
    {
        std::vector<uint16_t> c;
        Op21c(&c, kOpConst16, 2, 10);
        Op21c(&c, kOpConst16, 3, 20);
        Op21c(&c, kOpConst16, 4, 30);
        Op21c(&c, kOpConst16, 5, 40);
        Op21c(&c, kOpConst16, 6, 50);
        Op21c(&c, kOpConst16, 7, 60);
        OpFilledNewArrayRange(&c, kIntArr, 6, 2);
        c.push_back(Op11x(kOpMoveResultObject, 0));
        c.push_back(Op11n(kOpConst4, 1, 5));
        Op23x(&c, kOpAget, 8, 0, 1);         // v8 = a[5] = 60
        c.push_back(Op12x(kOpArrayLength, 9, 0));
        Op23x(&c, kOpAddInt, 8, 8, 9);       // 60 + 6 = 66
        c.push_back(Op11x(kOpReturn, 8));
        s.range_array.code = c;
        s.range_array.registers_size = 10;
    }
    // int nestedArray() { int[][] m = new int[][]{ new int[]{1,2}, new int[]{3} };
    //                     return m.length*10 + m[0].length; }
    // "[[I" must take its element width from component_type ("[I" -> pointer),
    // which is the case that breaks if the code slices the descriptor instead.
    {
        std::vector<uint16_t> c;
        c.push_back(Op11n(kOpConst4, 3, 1));
        c.push_back(Op11n(kOpConst4, 4, 2));
        OpFilledNewArray(&c, kIntArr, {3, 4});
        c.push_back(Op11x(kOpMoveResultObject, 1));   // v1 = {1,2}
        c.push_back(Op11n(kOpConst4, 3, 3));
        OpFilledNewArray(&c, kIntArr, {3});
        c.push_back(Op11x(kOpMoveResultObject, 2));   // v2 = {3}
        OpFilledNewArray(&c, kNestedArr, {1, 2});
        c.push_back(Op11x(kOpMoveResultObject, 0));   // v0 = {{1,2},{3}}
        c.push_back(Op11n(kOpConst4, 3, 0));
        Op23x(&c, kOpAgetObject, 4, 0, 3);            // v4 = m[0]
        c.push_back(Op12x(kOpArrayLength, 5, 4));     // v5 = 2
        c.push_back(Op12x(kOpArrayLength, 6, 0));     // v6 = 2
        Op23x(&c, kOpAddInt, 5, 5, 6);                // 2 + 2 = 4
        c.push_back(Op11x(kOpReturn, 5));
        s.nested_array.code = c;
        s.nested_array.registers_size = 7;
    }

    DexBuilder builder;
    const std::vector<uint8_t> dex = builder.Build(BuildClasses(s));

    kudroid::kuart::DexClassLinker linker;
    std::string error;
    if (!linker.AddDexFile(dex.data(), dex.size(), "filled_array.dex", &error)) {
        std::printf("  FAIL AddDexFile: %s\n", error.c_str());
        return 1;
    }
    kudroid::kuart::DexClass* klass = linker.FindClass("LT;");
    if (klass == nullptr) {
        std::printf("  FAIL FindClass(LT;)\n");
        return 1;
    }

    kudroid::kuart::Interpreter interp(&linker);

    const auto call = [&](const char* name, const char* sig) {
        interp.ClearPendingException();
        kudroid::kuart::DexMethod* m = klass->FindDirectMethod(name, sig);
        if (m == nullptr) {
            std::printf("  FAIL method not found: %s%s\n", name, sig);
            ++g_failures;
            return DexValue();
        }
        const DexValue v = interp.Execute(m, nullptr, 0);
        if (interp.HasPendingException()) {
            std::printf("  FAIL %s threw: %s\n", name, interp.last_error().c_str());
            ++g_failures;
        }
        return v;
    };

    // The whole point: this used to hit default: and throw
    // UnsupportedOperationException instead of producing an array.
    {
        const DexValue v = call("intArray", "()[I");
        auto* arr = static_cast<DexArray*>(v.l);
        Check(arr != nullptr, "intArray() returned an array (not null)");
        if (arr != nullptr) {
            Check(arr->length == 3, "intArray().length == 3");
            Check(arr->clazz != nullptr && arr->clazz->descriptor != nullptr &&
                      std::strcmp(arr->clazz->descriptor, "[I") == 0,
                  "intArray() element class is [I");
            if (arr->length == 3) {
                Check(arr->Get<int32_t>(0) == 7, "intArray()[0] == 7");
                Check(arr->Get<int32_t>(1) == 9, "intArray()[1] == 9");
                Check(arr->Get<int32_t>(2) == 11, "intArray()[2] == 11");
            }
        }
    }

    Check(call("intSum", "()I").i == 27, "intSum() == 27 (7+9+11, read back via aget)");
    Check(call("fiveElems", "()I").i == 10,
          "fiveElems() == 10 (a[4]=5 + length 5; 5-register k35c form)");
    Check(call("emptyArray", "()I").i == 0, "emptyArray() == 0 (zero-element form)");

    // Element width must follow component_type. Storing 4 bytes into a byte[]
    // would run past the allocation and corrupt neighbouring heap data.
    Check(call("byteArray", "()I").i == -1, "byteArray()[0] == -1 (byte sign extended)");
    Check(call("charArray", "()I").i == 0xFFFF, "charArray()[0] == 0xFFFF (char unsigned)");
    Check(call("shortArray", "()I").i == -2, "shortArray()[0] == -2 (short sign extended)");
    Check(call("longArray", "()J").j == 0x1122334455667788LL,
          "longArray()[0] == 0x1122334455667788 (wide element via /range)");

    {
        const DexValue v = call("stringArray", "()Ljava/lang/String;");
        auto* str = reinterpret_cast<kudroid::kuart::DexString*>(v.l);
        Check(str != nullptr && str->utf8 != nullptr && std::strcmp(str->utf8, "beta") == 0,
              "stringArray()[1] == \"beta\" (reference elements)");
    }

    Check(call("rangeArray", "()I").i == 66,
          "rangeArray() == 66 (a[5]=60 + length 6; /range with 6 elements)");
    Check(call("nestedArray", "()I").i == 4,
          "nestedArray() == 4 ([[I element width from component_type)");

    if (g_failures == 0) {
        std::printf("=== KuART filled-new-array test PASSED ===\n");
        return 0;
    }
    std::printf("=== KuART filled-new-array test FAILED (%d errors) ===\n", g_failures);
    return 1;
}
