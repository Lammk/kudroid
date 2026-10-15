// Host test for the JNI float/double ABI and for argument counts past the
// register budget. F/D values travel in a separate register file, which the old
// uintptr_t-only path dropped to silent 0s.
#include "kudroid/kuart/DexJniEnv.h"

#include <cmath>
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
using kudroid::kuart::DexValue;

// ACC_PUBLIC|ACC_STATIC|ACC_NATIVE — no code item.
constexpr uint32_t kAccPublicStaticNative = 0x1 | 0x8 | 0x100;

// ── natives under test ──────────────────────────────────────────────────────
// Each one is declared exactly as a real guest .so would declare it, so the
// compiler picks the genuine ABI registers rather than anything KuART chose.

jfloat JNICALL NatFloatId(JNIEnv*, jclass, jfloat v) { return v; }

jdouble JNICALL NatDoubleId(JNIEnv*, jclass, jdouble v) { return v; }

// Float return computed from float args: catches the case where args arrive in
// the right file but the return is read from the wrong one (or vice versa).
jfloat JNICALL NatFloatAdd(JNIEnv*, jclass, jfloat a, jfloat b) { return a + b; }

jdouble JNICALL NatDoubleAdd(JNIEnv*, jclass, jdouble a, jdouble b) { return a + b; }

// Mixed: ints and floats consume INDEPENDENT register budgets. If the packer
// used one shared counter, these would be shuffled.
jdouble JNICALL NatMixed(JNIEnv*, jclass, jint i, jdouble d, jint j, jfloat f) {
    return static_cast<jdouble>(i) + d + static_cast<jdouble>(j) + static_cast<jdouble>(f);
}

// Float arg, int return — the two files must not be confused with each other.
jint JNICALL NatFloatToInt(JNIEnv*, jclass, jfloat v) { return static_cast<jint>(v * 2.0f); }

// Int arg, float return.
jfloat JNICALL NatIntToFloat(JNIEnv*, jclass, jint v) { return static_cast<jfloat>(v) / 4.0f; }

// Four integer args: JNIEnv* + jclass + 4 == 6 GP registers, which fits the
// smaller of the two ABIs (SysV x86-64) and therefore every host.
jint JNICALL NatFourInts(JNIEnv*, jclass, jint a, jint b, jint c, jint d) {
    return a + b + c + d;
}

// Six integer args: JNIEnv* + jclass + 6 == 8 GP registers. Fits AAPCS64
// (x0-x7), which is the product target, but overflows SysV x86-64 (6 GP) where
// the host tests run. The assertion below is written against kJniGpRegs so the
// same test pins the correct behaviour on both: a real call on arm64, a loud
// failure on x86-64. The old code capped at 6 ARGUMENTS regardless of ABI and so
// rejected this even on arm64 where it is perfectly callable.
jint JNICALL NatSixInts(JNIEnv*, jclass, jint a, jint b, jint c, jint d, jint e, jint f) {
    return a + b + c + d + e + f;
}

// Eight float args: fills v0-v7 while the GP file stays nearly empty, proving
// the budgets really are separate.
jfloat JNICALL NatEightFloats(JNIEnv*, jclass, jfloat a, jfloat b, jfloat c, jfloat d,
                              jfloat e, jfloat f, jfloat g, jfloat h) {
    return a + b + c + d + e + f + g + h;
}

// Long arg alongside a double: both are 64-bit but live in different files.
jlong JNICALL NatLongDouble(JNIEnv*, jclass, jlong l, jdouble d) {
    return l + static_cast<jlong>(d);
}

struct Specs {
    MethodSpec object_ctor;

    MethodSpec float_id;
    MethodSpec double_id;
    MethodSpec float_add;
    MethodSpec double_add;
    MethodSpec mixed;
    MethodSpec float_to_int;
    MethodSpec int_to_float;
    MethodSpec four_ints;
    MethodSpec six_ints;
    MethodSpec eight_floats;
    MethodSpec long_double;
    MethodSpec unbound_float;  // never registered: must fail loudly, not return 0

    Specs() {
        object_ctor.name = "<init>";
        object_ctor.access_flags = 0x10001;

        float_id.name = "floatId";
        float_id.return_type = "F";
        float_id.params = {"F"};
        float_id.access_flags = kAccPublicStaticNative;

        double_id.name = "doubleId";
        double_id.return_type = "D";
        double_id.params = {"D"};
        double_id.access_flags = kAccPublicStaticNative;

        float_add.name = "floatAdd";
        float_add.return_type = "F";
        float_add.params = {"F", "F"};
        float_add.access_flags = kAccPublicStaticNative;

        double_add.name = "doubleAdd";
        double_add.return_type = "D";
        double_add.params = {"D", "D"};
        double_add.access_flags = kAccPublicStaticNative;

        mixed.name = "mixed";
        mixed.return_type = "D";
        mixed.params = {"I", "D", "I", "F"};
        mixed.access_flags = kAccPublicStaticNative;

        float_to_int.name = "floatToInt";
        float_to_int.return_type = "I";
        float_to_int.params = {"F"};
        float_to_int.access_flags = kAccPublicStaticNative;

        int_to_float.name = "intToFloat";
        int_to_float.return_type = "F";
        int_to_float.params = {"I"};
        int_to_float.access_flags = kAccPublicStaticNative;

        four_ints.name = "fourInts";
        four_ints.return_type = "I";
        four_ints.params = {"I", "I", "I", "I"};
        four_ints.access_flags = kAccPublicStaticNative;

        six_ints.name = "sixInts";
        six_ints.return_type = "I";
        six_ints.params = {"I", "I", "I", "I", "I", "I"};
        six_ints.access_flags = kAccPublicStaticNative;

        eight_floats.name = "eightFloats";
        eight_floats.return_type = "F";
        eight_floats.params = {"F", "F", "F", "F", "F", "F", "F", "F"};
        eight_floats.access_flags = kAccPublicStaticNative;

        long_double.name = "longDouble";
        long_double.return_type = "J";
        long_double.params = {"J", "D"};
        long_double.access_flags = kAccPublicStaticNative;

        unbound_float.name = "unboundFloat";
        unbound_float.return_type = "F";
        unbound_float.params = {"F"};
        unbound_float.access_flags = kAccPublicStaticNative;
    }
};

std::vector<ClassSpec> BuildClasses(const Specs& s) {
    ClassSpec object;
    object.descriptor = "Ljava/lang/Object;";
    object.superclass = "";
    object.direct_methods = {s.object_ctor};

    ClassSpec fp;
    fp.descriptor = "LFp;";
    fp.superclass = "Ljava/lang/Object;";
    fp.direct_methods = {s.float_id,     s.double_id,    s.float_add,   s.double_add,
                         s.mixed,        s.float_to_int, s.int_to_float, s.four_ints,
                         s.six_ints,     s.eight_floats, s.long_double, s.unbound_float};

    return {object, fp};
}

// float comparison with a tolerance: the values chosen below are all exactly
// representable, so this only guards against accidental narrowing.
bool NearF(float a, float b) { return std::fabs(a - b) < 1e-6f; }
bool NearD(double a, double b) { return std::fabs(a - b) < 1e-12; }

}  // namespace

int main() {
    std::printf("=== KuART JNI float/double ABI ===\n");

    Specs s;
    s.object_ctor.code = {static_cast<uint16_t>(0x0e)};  // return-void
    s.object_ctor.registers_size = 1;
    s.object_ctor.ins_size = 1;

    DexBuilder builder;
    const std::vector<uint8_t> dex = builder.Build(BuildClasses(s));

    kudroid::kuart::DexClassLinker linker;
    std::string error;
    if (!linker.AddDexFile(dex.data(), dex.size(), "fp.dex", &error)) {
        std::printf("  FAIL AddDexFile: %s\n=== FAILED ===\n", error.c_str());
        return 1;
    }
    kudroid::kuart::DexClass* klass = linker.FindClass("LFp;");
    if (klass == nullptr) {
        std::printf("  FAIL FindClass(LFp;)\n=== FAILED ===\n");
        return 1;
    }

    kudroid::kuart::Interpreter interp(&linker);
    kudroid::kuart::DexJniEnv jni(&linker, &interp);
    interp.set_jni_env(&jni);

    // Bind everything except unboundFloat.
    char n_fid[] = "floatId";        char s_fid[] = "(F)F";
    char n_did[] = "doubleId";       char s_did[] = "(D)D";
    char n_fadd[] = "floatAdd";      char s_fadd[] = "(FF)F";
    char n_dadd[] = "doubleAdd";     char s_dadd[] = "(DD)D";
    char n_mix[] = "mixed";          char s_mix[] = "(IDIF)D";
    char n_f2i[] = "floatToInt";     char s_f2i[] = "(F)I";
    char n_i2f[] = "intToFloat";     char s_i2f[] = "(I)F";
    char n_4i[] = "fourInts";        char s_4i[] = "(IIII)I";
    char n_6i[] = "sixInts";         char s_6i[] = "(IIIIII)I";
    char n_8f[] = "eightFloats";     char s_8f[] = "(FFFFFFFF)F";
    char n_ld[] = "longDouble";      char s_ld[] = "(JD)J";
    const JNINativeMethod natives[] = {
        {n_fid, s_fid, reinterpret_cast<void*>(&NatFloatId)},
        {n_did, s_did, reinterpret_cast<void*>(&NatDoubleId)},
        {n_fadd, s_fadd, reinterpret_cast<void*>(&NatFloatAdd)},
        {n_dadd, s_dadd, reinterpret_cast<void*>(&NatDoubleAdd)},
        {n_mix, s_mix, reinterpret_cast<void*>(&NatMixed)},
        {n_f2i, s_f2i, reinterpret_cast<void*>(&NatFloatToInt)},
        {n_i2f, s_i2f, reinterpret_cast<void*>(&NatIntToFloat)},
        {n_4i, s_4i, reinterpret_cast<void*>(&NatFourInts)},
        {n_6i, s_6i, reinterpret_cast<void*>(&NatSixInts)},
        {n_8f, s_8f, reinterpret_cast<void*>(&NatEightFloats)},
        {n_ld, s_ld, reinterpret_cast<void*>(&NatLongDouble)},
    };
    const jint num_natives = static_cast<jint>(sizeof(natives) / sizeof(natives[0]));
    Check(jni.RegisterNatives(klass, natives, num_natives) == JNI_OK, "RegisterNatives");

    // Calls go through Interpreter::Execute, which is the path a real
    // invoke-static takes: it routes native methods to DexJniEnv::CallNative.
    const auto call = [&](const char* name, const char* sig, std::vector<DexValue> args) {
        interp.ClearPendingException();
        kudroid::kuart::DexMethod* m = klass->FindDirectMethod(name, sig);
        if (m == nullptr) {
            std::printf("  FAIL method not found: %s%s\n", name, sig);
            ++g_failures;
            return DexValue();
        }
        const DexValue v = interp.Execute(m, args.data(), args.size());
        if (interp.HasPendingException()) {
            std::printf("  FAIL %s threw: %s\n", name, interp.last_error().c_str());
            ++g_failures;
        }
        return v;
    };

    // ── float/double arguments reach the native at all ──
    Check(NearF(call("floatId", "(F)F", {DexValue::Float(1.5f)}).f, 1.5f),
          "floatId(1.5f) == 1.5f (float arg + float return round-trip)");
    Check(NearF(call("floatId", "(F)F", {DexValue::Float(-0.25f)}).f, -0.25f),
          "floatId(-0.25f) == -0.25f (sign preserved)");
    Check(NearD(call("doubleId", "(D)D", {DexValue::Double(2.75)}).d, 2.75),
          "doubleId(2.75) == 2.75 (double arg + double return round-trip)");

    // A non-zero result here is the whole point: this used to return 0 silently.
    Check(call("floatId", "(F)F", {DexValue::Float(1.5f)}).f != 0.0f,
          "float return is not silently 0 (the old failure mode)");

    Check(NearF(call("floatAdd", "(FF)F", {DexValue::Float(1.25f), DexValue::Float(2.5f)}).f,
                3.75f),
          "floatAdd(1.25f, 2.5f) == 3.75f (two float args in v0,v1)");
    Check(NearD(call("doubleAdd", "(DD)D", {DexValue::Double(1.5), DexValue::Double(0.25)}).d,
                1.75),
          "doubleAdd(1.5, 0.25) == 1.75");

    // ── the two register files must not bleed into each other ──
    Check(call("floatToInt", "(F)I", {DexValue::Float(21.0f)}).i == 42,
          "floatToInt(21.0f) == 42 (float arg, int return)");
    Check(NearF(call("intToFloat", "(I)F", {DexValue::Int(10)}).f, 2.5f),
          "intToFloat(10) == 2.5f (int arg, float return)");

    // Interleaved: i->x2, d->v0, j->x3, f->v1. A shared counter would misplace these.
    Check(NearD(call("mixed", "(IDIF)D",
                     {DexValue::Int(1), DexValue::Double(0.5), DexValue::Int(2),
                      DexValue::Float(0.25f)})
                    .d,
                3.75),
          "mixed(1, 0.5, 2, 0.25f) == 3.75 (int and float budgets are independent)");

    Check(call("longDouble", "(JD)J", {DexValue::Long(1000), DexValue::Double(24.0)}).j == 1024,
          "longDouble(1000L, 24.0) == 1024 (64-bit int and 64-bit float differ)");

    // ── argument counts the old 6-argument cap rejected ──
    Check(call("fourInts", "(IIII)I",
               {DexValue::Int(1), DexValue::Int(2), DexValue::Int(3), DexValue::Int(4)})
              .i == 10,
          "fourInts(1..4) == 10 (6 GP registers: fits both ABIs)");

    // JNIEnv* and jclass occupy two GP registers, so the integer budget is
    // kJniGpRegs - 2. sixInts needs 8 GP: callable on AAPCS64, past the limit on
    // SysV x86-64. Either way the behaviour must be defined — never a silent 0.
    {
        const bool fits = kudroid::kuart::kJniGpRegs >= 8;
        interp.ClearPendingException();
        kudroid::kuart::DexMethod* m = klass->FindDirectMethod("sixInts", "(IIIIII)I");
        Check(m != nullptr, "found sixInts");
        std::vector<DexValue> args = {DexValue::Int(1), DexValue::Int(2), DexValue::Int(3),
                                      DexValue::Int(4), DexValue::Int(5), DexValue::Int(6)};
        const DexValue r = interp.Execute(m, args.data(), args.size());
        if (fits) {
            Check(!interp.HasPendingException() && r.i == 21,
                  "sixInts(1..6) == 21 (8 GP registers, AAPCS64)");
        } else {
            Check(interp.HasPendingException(),
                  "sixInts needs stack args on this ABI -> throws, never returns 0");
        }
        interp.ClearPendingException();
    }

    Check(NearF(call("eightFloats", "(FFFFFFFF)F",
                     {DexValue::Float(1.0f), DexValue::Float(2.0f), DexValue::Float(3.0f),
                      DexValue::Float(4.0f), DexValue::Float(5.0f), DexValue::Float(6.0f),
                      DexValue::Float(7.0f), DexValue::Float(8.0f)})
                    .f,
                36.0f),
          "eightFloats(1..8) == 36.0f (all 8 FP registers, GP file nearly empty)");

    // ── an unbound native must fail loudly ──
    // Returning 0 for a missing symbol is exactly the class of silent-wrong-value
    // bug this file exists to prevent.
    {
        interp.ClearPendingException();
        kudroid::kuart::DexMethod* m = klass->FindDirectMethod("unboundFloat", "(F)F");
        Check(m != nullptr, "found unboundFloat");
        DexValue arg[1] = {DexValue::Float(1.0f)};
        interp.Execute(m, arg, 1);
        Check(interp.HasPendingException(),
              "unbound native float method throws instead of returning 0");
        interp.ClearPendingException();
    }

    if (g_failures == 0) {
        std::printf("=== KuART JNI float/double test PASSED ===\n");
        return 0;
    }
    std::printf("=== KuART JNI float/double test FAILED (%d errors) ===\n", g_failures);
    return 1;
}
