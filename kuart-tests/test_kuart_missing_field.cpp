// Probe: an unresolvable field reference must name itself.
//
// A missing field cannot be auto-stubbed the way a missing method can — object
// layout is fixed once LinkClass has run, so there is nowhere to put the storage —
// which makes the diagnostic the entire remedy. It used to throw NoSuchFieldError
// whose message was the bare opcode name, "iput", identifying neither the class nor
// the field, and nothing reached classes.log because the declaring class was
// present. That is the shape that stopped Minecraft in onCreate: GameActivity did
// `new EditorInfo()` (fine, real class) then `iput inputType` (fatal, the class
// declared zero fields).
#include "kudroid/kuart/DexClassLinker.h"
#include "kudroid/kuart/DexJniEnv.h"
#include "kudroid/kuart/DexObject.h"
#include "kudroid/kuart/Interpreter.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "dex_builder.h"

namespace {

using dexbuild::ClassSpec;
using dexbuild::DexBuilder;
using dexbuild::FieldRefSpec;
using dexbuild::FieldSpec;
using dexbuild::MethodSpec;

int g_failures = 0;

void Check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "  OK  " : "  FAIL", what.c_str());
    if (!ok) ++g_failures;
}

constexpr uint8_t kOpConst4 = 0x12;
constexpr uint8_t kOpNewInstance = 0x22;
constexpr uint8_t kOpInvokeDirect = 0x70;
constexpr uint8_t kOpIget = 0x52;
constexpr uint8_t kOpIput = 0x59;
constexpr uint8_t kOpReturn = 0x0f;
constexpr uint8_t kOpReturnVoid = 0x0e;

uint16_t Op(uint8_t opcode, uint8_t operand) {
    return static_cast<uint16_t>(opcode | (operand << 8));
}

// Holder declares ONE field, `present`. `missingField` is only ever referenced,
// never declared — exactly how an app references a framework field.
struct Specs {
    FieldSpec present{"present", "I", 0x1};
    FieldRefSpec missing{"LHolder;", "missingField", "I"};

    MethodSpec holder_ctor;
    MethodSpec write_missing;
    MethodSpec read_missing;
    MethodSpec write_present;

    Specs() {
        holder_ctor.name = "<init>";
        holder_ctor.access_flags = 0x10001;  // public constructor

        write_missing.name = "writeMissing";
        write_missing.access_flags = 0x9;  // public static
        read_missing.name = "readMissing";
        read_missing.return_type = "I";
        read_missing.access_flags = 0x9;
        write_present.name = "writePresent";
        write_present.return_type = "I";
        write_present.access_flags = 0x9;
    }
};

std::vector<ClassSpec> BuildClasses(const Specs& s) {
    ClassSpec object;
    object.descriptor = "Ljava/lang/Object;";
    // Object has no superclass. Leaving the default ("Ljava/lang/Object;") makes it
    // its own parent, and LinkClass then recurses until the stack overflows.
    object.superclass = "";
    MethodSpec object_ctor;
    object_ctor.name = "<init>";
    object_ctor.access_flags = 0x10001;
    object_ctor.registers_size = 1;
    object_ctor.ins_size = 1;
    object_ctor.code = {Op(kOpReturnVoid, 0)};
    object.direct_methods = {object_ctor};

    ClassSpec holder;
    holder.descriptor = "LHolder;";
    holder.instance_fields = {s.present};
    holder.direct_methods = {s.holder_ctor};

    ClassSpec reader;
    reader.descriptor = "LReader;";
    reader.direct_methods = {s.write_missing, s.read_missing, s.write_present};
    reader.extra_types = {"LHolder;"};
    reader.extra_field_refs = {s.missing};

    return {object, holder, reader};
}

}  // namespace

using kudroid::kuart::DexClass;
using kudroid::kuart::DexClassLinker;
using kudroid::kuart::DexJniEnv;
using kudroid::kuart::DexMethod;
using kudroid::kuart::DexValue;
using kudroid::kuart::Interpreter;

int main() {
    std::printf("=== KuART missing-field diagnostics ===\n");

    // Pass 1: learn the indices the bytecode has to encode.
    Specs probe;
    DexBuilder index_builder;
    index_builder.Build(BuildClasses(probe));

    const uint16_t kHolderType =
        static_cast<uint16_t>(index_builder.TypeIndexOf("LHolder;"));
    const uint16_t kFieldPresent =
        static_cast<uint16_t>(index_builder.FieldIndexOf("LHolder;", probe.present));
    const uint16_t kFieldMissing =
        static_cast<uint16_t>(index_builder.FieldRefIndexOf(probe.missing));
    const uint16_t kHolderCtor =
        static_cast<uint16_t>(index_builder.MethodIndexOf("LHolder;", probe.holder_ctor));
    const uint16_t kObjectCtorIdx = [&] {
        MethodSpec object_ctor;
        object_ctor.name = "<init>";
        object_ctor.access_flags = 0x10001;
        return static_cast<uint16_t>(
            index_builder.MethodIndexOf("Ljava/lang/Object;", object_ctor));
    }();

    Specs s;

    // Holder.<init>: super() then return.
    s.holder_ctor.registers_size = 1;
    s.holder_ctor.ins_size = 1;
    s.holder_ctor.code = {
        Op(kOpInvokeDirect, 0x10), kObjectCtorIdx, 0x0000,
        Op(kOpReturnVoid, 0),
    };

    // writeMissing(): new Holder, then iput into a field that does not exist.
    s.write_missing.registers_size = 2;
    s.write_missing.code = {
        Op(kOpNewInstance, 0), kHolderType,
        Op(kOpInvokeDirect, 0x10), kHolderCtor, 0x0000,
        Op(kOpConst4, 0x51),  // v1 = 5
        static_cast<uint16_t>(kOpIput | (0x01 << 8) | (0x00 << 12)), kFieldMissing,
        Op(kOpReturnVoid, 0),
    };

    // readMissing(): same field, read side, so the opcode in the message differs.
    s.read_missing.registers_size = 2;
    s.read_missing.code = {
        Op(kOpNewInstance, 0), kHolderType,
        Op(kOpInvokeDirect, 0x10), kHolderCtor, 0x0000,
        static_cast<uint16_t>(kOpIget | (0x01 << 8) | (0x00 << 12)), kFieldMissing,
        Op(kOpReturn, 1),
    };

    // writePresent(): the field that IS declared must still work.
    s.write_present.registers_size = 2;
    s.write_present.code = {
        Op(kOpNewInstance, 0), kHolderType,
        Op(kOpInvokeDirect, 0x10), kHolderCtor, 0x0000,
        Op(kOpConst4, 0x71),  // v1 = 7
        static_cast<uint16_t>(kOpIput | (0x01 << 8) | (0x00 << 12)), kFieldPresent,
        static_cast<uint16_t>(kOpIget | (0x01 << 8) | (0x00 << 12)), kFieldPresent,
        Op(kOpReturn, 1),
    };

    DexBuilder builder;
    const std::vector<uint8_t> dex = builder.Build(BuildClasses(s));
    std::printf("DEX: %zu bytes\n", dex.size());

    DexClassLinker linker;
    std::string error;
    if (!linker.AddDexFile(dex.data(), dex.size(), "test.dex", &error)) {
        std::printf("  FAIL AddDexFile: %s\n=== FAILED ===\n", error.c_str());
        return 1;
    }

    Interpreter interp(&linker);
    DexJniEnv jni(&linker, &interp);
    interp.set_jni_env(&jni);

    DexClass* reader = linker.FindClass("LReader;");
    if (reader == nullptr) {
        std::printf("  FAIL FindClass(LReader;): %s\n=== FAILED ===\n",
                    linker.last_error().c_str());
        return 1;
    }

    // Sanity: the declaring class resolves and really lacks the field, so the test
    // is exercising the intended path rather than a broken DEX.
    DexClass* holder = linker.FindClass("LHolder;");
    Check(holder != nullptr && !holder->is_stub, "Holder resolves as a real class");
    if (holder != nullptr) {
        Check(holder->FindInstanceField("missingField", "I") == nullptr,
              "Holder genuinely lacks missingField");
        Check(holder->FindInstanceField("present", "I") != nullptr,
              "Holder declares present");
    }

    DexMethod* m = reader->FindDirectMethod("writeMissing", "()V");
    if (m == nullptr) {
        std::printf("  FAIL no writeMissing()\n=== FAILED ===\n");
        return 1;
    }

    interp.ClearPendingException();
    interp.Execute(m, nullptr, 0);
    Check(interp.HasPendingException(), "an unresolvable iput still throws");

    const std::string err = interp.last_error();
    std::printf("  message: %s\n", err.c_str());

    Check(err.find("NoSuchFieldError") != std::string::npos, "throws NoSuchFieldError");
    // The whole point: the message says what is missing.
    Check(err.find("Holder") != std::string::npos, "message names the declaring class");
    Check(err.find("missingField") != std::string::npos, "message names the field");
    Check(err.find(": I") != std::string::npos, "message carries the type descriptor");
    // Without the opcode the site is ambiguous when a method both reads and writes.
    Check(err.find("iput") != std::string::npos, "message names the opcode");

    const std::string trace = interp.pending_exception_trace();
    Check(trace.find("Reader.writeMissing") != std::string::npos,
          "stack trace names the faulting method");
    interp.ClearPendingException();

    // A read of the same absent field must report as a read.
    DexMethod* r = reader->FindDirectMethod("readMissing", "()I");
    if (r != nullptr) {
        interp.ClearPendingException();
        interp.Execute(r, nullptr, 0);
        Check(interp.HasPendingException(), "an unresolvable iget throws too");
        Check(interp.last_error().find("iget") != std::string::npos,
              "read reports iget, not iput");
        interp.ClearPendingException();
    }

    // A field that exists must be unaffected: the diagnostics must not have turned a
    // working access into a failure.
    DexMethod* ok = reader->FindDirectMethod("writePresent", "()I");
    if (ok != nullptr) {
        interp.ClearPendingException();
        const DexValue v = interp.Execute(ok, nullptr, 0);
        Check(!interp.HasPendingException(),
              "a field that exists is still written and read back");
        Check(v.i == 7, std::string("round-trips the value, got ") + std::to_string(v.i));
        interp.ClearPendingException();
    }

    if (g_failures == 0) {
        std::printf("=== KuART missing-field test PASSED ===\n");
        return 0;
    }
    std::printf("=== KuART missing-field test FAILED (%d) ===\n", g_failures);
    return 1;
}
