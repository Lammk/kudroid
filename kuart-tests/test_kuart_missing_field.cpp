// Probe: an unresolvable field or method reference must name itself.
//
// A missing field cannot be auto-stubbed the way a missing method can — object
// layout is fixed once LinkClass has run, so there is nowhere to put the storage —
// which makes the diagnostic the entire remedy. It used to throw NoSuchFieldError
// whose message was the bare opcode name, "iput", identifying neither the class nor
// the field, and nothing reached classes.log because the declaring class was
// present. That is the shape that stopped Minecraft in onCreate: GameActivity did
// `new EditorInfo()` (fine, real class) then `iput inputType` (fatal, the class
// declared zero fields).
//
// The method half had the same defect and is covered here too. An unresolvable
// invoke threw "failed to resolve method index 45364" — an index meaningful only
// inside one DEX file, naming neither class nor method. GameActivity.onCreate hit it
// on the line after it found libminecraftpe.so, so the app reached the point of
// loading its renderer and then failed with nothing to act on.
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
using dexbuild::MethodRefSpec;
using dexbuild::MethodSpec;

int g_failures = 0;

void Check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "  OK  " : "  FAIL", what.c_str());
    if (!ok) ++g_failures;
}

constexpr uint8_t kOpConst4 = 0x12;
constexpr uint8_t kOpNewInstance = 0x22;
constexpr uint8_t kOpInvokeDirect = 0x70;
constexpr uint8_t kOpInvokeVirtual = 0x6e;
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
    // Same shape for a method: referenced, never declared. Named after the AGDK
    // method that produced the original failure.
    MethodRefSpec missing_method{"LHolder;", "initializeNativeCode", "J", {"I"}};

    MethodSpec holder_ctor;
    MethodSpec write_missing;
    MethodSpec read_missing;
    MethodSpec write_present;
    MethodSpec call_missing;   // invoke-virtual a method Holder does not declare
    MethodSpec call_present;   // the declared method must still work

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
        call_missing.name = "callMissing";
        call_missing.access_flags = 0x9;
        call_present.name = "callPresent";
        call_present.access_flags = 0x9;
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

    // The interpreter allocates the exception object, so the class has to exist or the
    // throw falls back to a placeholder and the message is never attached.
    ClassSpec throwable;
    throwable.descriptor = "Ljava/lang/Throwable;";
    throwable.instance_fields = {FieldSpec{"message", "Ljava/lang/String;", 0x1}};

    ClassSpec no_such_field;
    no_such_field.descriptor = "Ljava/lang/NoSuchFieldError;";
    no_such_field.superclass = "Ljava/lang/Throwable;";

    ClassSpec no_such_method;
    no_such_method.descriptor = "Ljava/lang/NoSuchMethodError;";
    no_such_method.superclass = "Ljava/lang/Throwable;";

    ClassSpec string;
    string.descriptor = "Ljava/lang/String;";

    ClassSpec reader;
    reader.descriptor = "LReader;";
    reader.direct_methods = {s.write_missing, s.read_missing, s.write_present,
                             s.call_missing, s.call_present};
    reader.extra_types = {"LHolder;"};
    reader.extra_field_refs = {s.missing};
    reader.extra_method_refs = {s.missing_method};

    return {object, string, throwable, no_such_field, no_such_method, holder, reader};
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
    const uint16_t kMethodMissing =
        static_cast<uint16_t>(index_builder.MethodRefIndexOf(probe.missing_method));
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

    // callMissing(): new Holder, then invoke-virtual a method it does not declare.
    // This is the GameActivity.onCreate shape — the object is fine, the method is not.
    s.call_missing.registers_size = 2;
    s.call_missing.outs_size = 2;
    s.call_missing.code = {
        Op(kOpNewInstance, 0), kHolderType,
        Op(kOpInvokeDirect, 0x10), kHolderCtor, 0x0000,
        Op(kOpConst4, 0x11),  // v1 = 1, the int argument
        Op(kOpInvokeVirtual, 0x20), kMethodMissing, 0x0010,  // {v0, v1}
        Op(kOpReturnVoid, 0),
    };

    // callPresent(): the constructor resolves, so an invoke that CAN be resolved must
    // still run. Without this the checks could be rejecting every invoke.
    s.call_present.registers_size = 1;
    s.call_present.outs_size = 1;
    s.call_present.code = {
        Op(kOpNewInstance, 0), kHolderType,
        Op(kOpInvokeDirect, 0x10), kHolderCtor, 0x0000,
        Op(kOpReturnVoid, 0),
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

    // ── the method half ──
    //
    // An unresolvable invoke used to throw "failed to resolve method index 45364".
    // The index identifies a slot in one DEX file and nothing else: it named neither
    // the class nor the method, so a NoSuchMethodError could not be acted on without
    // disassembling the APK first. GameActivity.onCreate threw exactly this.
    DexMethod* cm = reader->FindDirectMethod("callMissing", "()V");
    Check(cm != nullptr, "callMissing resolved");
    if (cm != nullptr) {
        Check(holder == nullptr ||
                  holder->FindVirtualMethod("initializeNativeCode", "(I)J") == nullptr,
              "Holder genuinely lacks initializeNativeCode");

        interp.ClearPendingException();
        interp.Execute(cm, nullptr, 0);
        Check(interp.HasPendingException(), "an unresolvable invoke throws");

        const std::string merr = interp.last_error();
        std::printf("  message: %s\n", merr.c_str());

        Check(merr.find("NoSuchMethodError") != std::string::npos,
              "throws NoSuchMethodError");
        Check(merr.find("Holder") != std::string::npos,
              "message names the declaring class: " + merr);
        Check(merr.find("initializeNativeCode") != std::string::npos,
              "message names the method: " + merr);
        // The signature distinguishes overloads, which is the whole reason a name
        // alone is not enough to know what to add.
        Check(merr.find("(I)J") != std::string::npos,
              "message carries the signature: " + merr);
        // The index was the entire old message and is meaningless outside one DEX.
        Check(merr.find("method index") == std::string::npos,
              "message is not a bare DEX index: " + merr);

        const std::string mtrace = interp.pending_exception_trace();
        Check(mtrace.find("Reader.callMissing") != std::string::npos,
              "stack trace names the faulting method");
        interp.ClearPendingException();
    }

    // An invoke that CAN resolve must be unaffected.
    DexMethod* cp = reader->FindDirectMethod("callPresent", "()V");
    if (cp != nullptr) {
        interp.ClearPendingException();
        interp.Execute(cp, nullptr, 0);
        const bool threw = interp.HasPendingException();
        Check(!threw, std::string("a resolvable invoke still runs") +
                          (threw ? ": " + interp.last_error() : ""));
        interp.ClearPendingException();
    }

    if (g_failures == 0) {
        std::printf("=== KuART missing-field test PASSED ===\n");
        return 0;
    }
    std::printf("=== KuART missing-field test FAILED (%d) ===\n", g_failures);
    return 1;
}