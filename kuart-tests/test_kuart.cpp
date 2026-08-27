// Host test cho KuART: nạp class từ DEX, dựng field layout + vtable.
//
// Đây là thứ thay thế toàn bộ chuỗi dex2jar → classes.jar → AutoStub → Avian:
// DEX vào thẳng, ra DexClass dùng được ngay.
#include "kudroid/kuart/DexClassLinker.h"

#include <cstdio>
#include <cstring>
#include <string>

#include "dex_builder.h"

namespace {

int g_failures = 0;

void Check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "  OK  " : "  FAIL", what.c_str());
    if (!ok) ++g_failures;
}

using namespace dexbuild;

// Cây class để test: Object <- Base <- Derived, Derived implements Runnable.
//
//   Object  : không field, không method (đứng làm gốc)
//   Base    : int baseInt; long baseLong; Object baseRef
//             static int counter
//             void run()      -> Derived override
//             int onlyInBase()
//   Derived : byte derivedByte; int derivedInt
//             void run()      (override)
//             int derivedOnly()
std::vector<ClassSpec> MakeClassTree() {
    ClassSpec object;
    object.descriptor = "Ljava/lang/Object;";
    object.superclass = "";  // gốc, không superclass
    {
        MethodSpec ctor;
        ctor.name = "<init>";
        ctor.access_flags = 0x10001;  // ACC_PUBLIC | ACC_CONSTRUCTOR
        ctor.code = {0x000e};         // return-void
        ctor.registers_size = 1;
        ctor.ins_size = 1;
        object.direct_methods.push_back(ctor);
    }

    ClassSpec runnable;
    runnable.descriptor = "Ljava/lang/Runnable;";
    runnable.superclass = "Ljava/lang/Object;";
    runnable.access_flags = 0x601;  // ACC_PUBLIC | ACC_INTERFACE | ACC_ABSTRACT
    {
        MethodSpec run;
        run.name = "run";
        run.access_flags = 0x401;  // ACC_PUBLIC | ACC_ABSTRACT
        runnable.virtual_methods.push_back(run);
    }

    ClassSpec base;
    base.descriptor = "LBase;";
    base.superclass = "Ljava/lang/Object;";
    base.static_fields.push_back({"counter", "I", 0x9});  // PUBLIC | STATIC
    base.instance_fields.push_back({"baseInt", "I", 0x1});
    base.instance_fields.push_back({"baseLong", "J", 0x1});
    base.instance_fields.push_back({"baseRef", "Ljava/lang/Object;", 0x1});
    {
        MethodSpec ctor;
        ctor.name = "<init>";
        ctor.access_flags = 0x10001;
        ctor.code = {0x000e};
        ctor.registers_size = 1;
        ctor.ins_size = 1;
        base.direct_methods.push_back(ctor);

        MethodSpec run;
        run.name = "run";
        run.code = {0x000e};
        run.registers_size = 1;
        run.ins_size = 1;
        base.virtual_methods.push_back(run);

        MethodSpec only;
        only.name = "onlyInBase";
        only.return_type = "I";
        only.code = {0x0012, 0x000f};  // const/4 v0, 0; return v0
        only.registers_size = 2;
        only.ins_size = 1;
        base.virtual_methods.push_back(only);
    }

    ClassSpec derived;
    derived.descriptor = "LDerived;";
    derived.superclass = "LBase;";
    derived.interfaces.push_back("Ljava/lang/Runnable;");
    derived.instance_fields.push_back({"derivedByte", "B", 0x1});
    derived.instance_fields.push_back({"derivedInt", "I", 0x1});
    {
        MethodSpec ctor;
        ctor.name = "<init>";
        ctor.access_flags = 0x10001;
        ctor.code = {0x000e};
        ctor.registers_size = 1;
        ctor.ins_size = 1;
        derived.direct_methods.push_back(ctor);

        MethodSpec run;  // override Base.run
        run.name = "run";
        run.code = {0x000e};
        run.registers_size = 1;
        run.ins_size = 1;
        derived.virtual_methods.push_back(run);

        MethodSpec only;
        only.name = "derivedOnly";
        only.return_type = "I";
        only.params.push_back("J");  // kiểm tra arg_words của long
        only.code = {0x0012, 0x000f};
        only.registers_size = 4;
        only.ins_size = 3;
        derived.virtual_methods.push_back(only);
    }

    return {object, runnable, base, derived};
}

}  // namespace

int main() {
    std::printf("=== KuART: nạp class từ DEX (không qua dex2jar/JAR) ===\n");

    DexBuilder builder;
    const std::vector<uint8_t> dex = builder.Build(MakeClassTree());
    std::printf("DEX synthetic: %zu bytes\n", dex.size());

    kudroid::kuart::DexClassLinker linker;
    std::string error;
    if (!linker.AddDexFile(dex.data(), dex.size(), "test.dex", &error)) {
        std::printf("  FAIL AddDexFile: %s\n", error.c_str());
        std::printf("=== KuART test FAILED ===\n");
        return 1;
    }
    Check(linker.NumDexFiles() == 1, "mở được 1 DEX");

    // ── resolve ──
    kudroid::kuart::DexClass* derived = linker.FindClass("LDerived;");
    Check(derived != nullptr, "FindClass(LDerived;)");
    if (derived == nullptr) {
        std::printf("last_error: %s\n=== KuART test FAILED ===\n",
                    linker.last_error().c_str());
        return 1;
    }

    Check(derived->PrettyName() == "Derived", "PrettyName == Derived");
    Check(derived->status == kudroid::kuart::DexClass::Status::kLinked, "trạng thái kLinked");

    // ── chuỗi kế thừa ──
    kudroid::kuart::DexClass* base = derived->superclass;
    Check(base != nullptr && std::strcmp(base->descriptor, "LBase;") == 0,
          "superclass của Derived là Base");
    Check(base != nullptr && base->superclass != nullptr &&
              std::strcmp(base->superclass->descriptor, "Ljava/lang/Object;") == 0,
          "superclass của Base là Object");
    Check(derived->interfaces.size() == 1 && derived->interfaces[0] != nullptr &&
              std::strcmp(derived->interfaces[0]->descriptor, "Ljava/lang/Runnable;") == 0,
          "Derived implements Runnable");
    Check(derived->IsSubClassOf(base), "IsSubClassOf(Base)");
    Check(derived->IsSubClassOf(derived->interfaces[0]), "IsSubClassOf(Runnable)");

    // ── cache: gọi lại phải trả cùng con trỏ ──
    Check(linker.FindClass("LDerived;") == derived, "FindClass cache trả cùng con trỏ");

    // ── field layout ──
    // Base: long 8B trước (offset 0), rồi ref 8B (offset 8), rồi int 4B (16) = 20
    kudroid::kuart::DexField* base_long = base->FindInstanceField("baseLong", "J");
    kudroid::kuart::DexField* base_ref = base->FindInstanceField("baseRef", "Ljava/lang/Object;");
    kudroid::kuart::DexField* base_int = base->FindInstanceField("baseInt", "I");
    Check(base_long != nullptr && base_ref != nullptr && base_int != nullptr,
          "tìm được 3 field instance của Base");
    if (base_long != nullptr && base_ref != nullptr && base_int != nullptr) {
        std::printf("    Base: baseLong@%u baseRef@%u baseInt@%u object_size=%u\n",
                    base_long->offset_or_slot, base_ref->offset_or_slot,
                    base_int->offset_or_slot, base->object_size);
        Check(base_long->offset_or_slot % 8 == 0, "baseLong align 8");
        Check(base_int->offset_or_slot % 4 == 0, "baseInt align 4");
        // Field không được chồng nhau.
        Check(base_long->offset_or_slot != base_ref->offset_or_slot &&
                  base_ref->offset_or_slot != base_int->offset_or_slot,
              "field Base không chồng offset");
    }

    // Field của Derived phải nằm SAU field của Base.
    kudroid::kuart::DexField* derived_int = derived->FindInstanceField("derivedInt", "I");
    Check(derived_int != nullptr && derived_int->offset_or_slot >= base->object_size,
          "field của Derived nằm sau field kế thừa");
    Check(derived->object_size > base->object_size, "object_size của Derived lớn hơn Base");
    std::printf("    Derived: derivedInt@%u object_size=%u\n",
                derived_int != nullptr ? derived_int->offset_or_slot : 0,
                derived->object_size);

    // Field kế thừa nhìn thấy được từ subclass, cùng offset.
    kudroid::kuart::DexField* inherited = derived->FindInstanceField("baseInt", "I");
    Check(inherited == base_int, "Derived thấy baseInt kế thừa, cùng offset");

    // ── field static ──
    kudroid::kuart::DexField* counter = base->FindStaticField("counter", "I");
    Check(counter != nullptr && counter->IsStatic(), "field static counter");
    Check(base->static_values.size() == 1, "static_values có 1 ô");

    // ── vtable ──
    // Base có run + onlyInBase = 2 slot. Derived override run (không thêm slot)
    // và thêm derivedOnly = 3 slot.
    std::printf("    vtable Base=%zu Derived=%zu\n", base->vtable.size(),
                derived->vtable.size());
    Check(base->vtable.size() == 2, "vtable Base có 2 slot");
    Check(derived->vtable.size() == 3, "vtable Derived có 3 slot (override không thêm slot)");

    kudroid::kuart::DexMethod* base_run = base->FindVirtualMethod("run", "()V");
    kudroid::kuart::DexMethod* derived_run = derived->FindVirtualMethod("run", "()V");
    Check(base_run != nullptr && derived_run != nullptr, "tìm được run() ở cả hai class");
    Check(base_run != derived_run, "run() của Derived khác của Base");
    if (base_run != nullptr && derived_run != nullptr) {
        Check(base_run->vtable_index == derived_run->vtable_index,
              "override dùng CÙNG vtable slot");
        Check(derived->vtable[derived_run->vtable_index] == derived_run,
              "vtable Derived trỏ tới bản override");
    }

    // Method chỉ có ở Base vẫn gọi được qua Derived, giữ nguyên slot.
    kudroid::kuart::DexMethod* only_in_base = derived->FindVirtualMethod("onlyInBase", "()I");
    Check(only_in_base != nullptr, "Derived thấy onlyInBase kế thừa");
    if (only_in_base != nullptr) {
        Check(derived->vtable[only_in_base->vtable_index] == only_in_base,
              "slot kế thừa giữ nguyên method của Base");
    }

    // ── metadata method ──
    kudroid::kuart::DexMethod* derived_only = derived->FindVirtualMethod("derivedOnly", "(J)I");
    Check(derived_only != nullptr, "tìm được derivedOnly(J)I");
    if (derived_only != nullptr) {
        // this(1) + long(2) = 3
        Check(derived_only->arg_words == 3, "arg_words == 3 (this + long chiếm 2)");
        Check(derived_only->registers_size == 4, "registers_size == 4");
        Check(derived_only->code_item != nullptr, "có code_item");
    }

    kudroid::kuart::DexMethod* ctor = derived->FindDirectMethod("<init>", "()V");
    Check(ctor != nullptr && ctor->IsConstructor(), "constructor là direct method");
    Check(ctor != nullptr && ctor->vtable_index == kudroid::kuart::DexMethod::kInvalidVTableIndex,
          "constructor không vào vtable");

    // ── cấp phát object ──
    kudroid::kuart::DexObject* obj = linker.AllocObject(derived);
    Check(obj != nullptr && obj->clazz == derived, "AllocObject(Derived)");
    if (obj != nullptr && base_int != nullptr && base_long != nullptr &&
        derived_int != nullptr) {
        obj->SetField<int32_t>(base_int->offset_or_slot, 0x11223344);
        obj->SetField<int64_t>(base_long->offset_or_slot, 0x1122334455667788LL);
        obj->SetField<int32_t>(derived_int->offset_or_slot, 0x55667788);
        Check(obj->GetField<int32_t>(base_int->offset_or_slot) == 0x11223344,
              "đọc/ghi field int kế thừa");
        Check(obj->GetField<int64_t>(base_long->offset_or_slot) == 0x1122334455667788LL,
              "đọc/ghi field long (không lệch align)");
        Check(obj->GetField<int32_t>(derived_int->offset_or_slot) == 0x55667788,
              "đọc/ghi field của subclass");
        // Ghi field này không được đè field kia.
        Check(obj->GetField<int32_t>(base_int->offset_or_slot) == 0x11223344,
              "field không đè lẫn nhau");
    }

    // ── class nguyên thủy + mảng ──
    kudroid::kuart::DexClass* int_class = linker.FindClass("I");
    Check(int_class != nullptr && int_class->is_primitive, "class nguyên thủy I");

    kudroid::kuart::DexClass* int_array = linker.FindClass("[I");
    Check(int_array != nullptr && int_array->is_array, "class mảng [I");
    Check(int_array != nullptr && int_array->component_type == int_class,
          "component_type của [I là I");

    kudroid::kuart::DexClass* nested = linker.FindClass("[[I");
    Check(nested != nullptr && nested->is_array && nested->component_type == int_array,
          "mảng lồng [[I có component [I");

    kudroid::kuart::DexArray* arr = linker.AllocArray(int_array, 4);
    Check(arr != nullptr && arr->length == 4, "AllocArray([I, 4)");
    if (arr != nullptr) {
        for (int32_t i = 0; i < 4; ++i) arr->Set<int32_t>(i, i * 100);
        bool all_ok = true;
        for (int32_t i = 0; i < 4; ++i) {
            if (arr->Get<int32_t>(i) != i * 100) all_ok = false;
        }
        Check(all_ok, "đọc/ghi phần tử mảng int");
    }

    std::printf("heap: %zu bytes, %zu block, %zu class đã nạp\n",
                linker.heap().BytesAllocated(), linker.heap().BlockCount(),
                linker.NumLoadedClasses());

    if (g_failures == 0) {
        std::printf("=== KuART test PASSED ===\n");
        return 0;
    }
    std::printf("=== KuART test FAILED (%d lỗi) ===\n", g_failures);
    return 1;
}
