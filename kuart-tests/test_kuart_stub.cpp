// Host test for auto-stub policy: boot-classpath classes get a placeholder;
// app packages must fail to resolve, and using a stub must fail loudly.
#include "kudroid/kuart/DexJniEnv.h"
#include "kudroid/kuart/DexReflect.h"

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

uint16_t Op11x(uint8_t op, uint8_t a) { return static_cast<uint16_t>(op | (a << 8)); }
void Op21c(std::vector<uint16_t>* code, uint8_t op, uint8_t a, uint16_t idx) {
    code->push_back(static_cast<uint16_t>(op | (a << 8)));
    code->push_back(idx);
}

constexpr uint8_t kOpReturnVoid = 0x0e;
constexpr uint8_t kOpReturnObject = 0x11;
constexpr uint8_t kOpNewInstance = 0x22;

struct Specs {
    MethodSpec object_ctor;
    MethodSpec make_stub;  // new-instance on a class that only exists as a stub

    Specs() {
        object_ctor.name = "<init>";
        object_ctor.access_flags = 0x10001;

        make_stub.name = "makeStub";
        make_stub.return_type = "Ljava/lang/Object;";
        make_stub.access_flags = 0x9;
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

    ClassSpec class_class;
    class_class.descriptor = "Ljava/lang/Class;";
    class_class.superclass = "Ljava/lang/Object;";

    ClassSpec t;
    t.descriptor = "LT;";
    t.superclass = "Ljava/lang/Object;";
    // Referenced by bytecode only: a boot-classpath class this DEX does not
    // define, so resolving it produces a stub.
    t.extra_types = {"Landroid/nowhere/Ghost;"};
    t.direct_methods = {s.make_stub};

    return {object, string, class_class, t};
}

}  // namespace

int main() {
    std::printf("=== KuART auto-stub policy ===\n");

    Specs probe;
    DexBuilder index_builder;
    index_builder.Build(BuildClasses(probe));
    const uint16_t kGhostType =
        static_cast<uint16_t>(index_builder.TypeIndexOf("Landroid/nowhere/Ghost;"));

    Specs s;
    s.object_ctor.code = {Op11x(kOpReturnVoid, 0)};
    s.object_ctor.registers_size = 1;
    s.object_ctor.ins_size = 1;

    // Object makeStub() { return new android.nowhere.Ghost(); }
    {
        std::vector<uint16_t> c;
        Op21c(&c, kOpNewInstance, 0, kGhostType);
        c.push_back(Op11x(kOpReturnObject, 0));
        s.make_stub.code = c;
        s.make_stub.registers_size = 1;
    }

    DexBuilder builder;
    const std::vector<uint8_t> dex = builder.Build(BuildClasses(s));

    kudroid::kuart::DexClassLinker linker;
    std::string error;
    if (!linker.AddDexFile(dex.data(), dex.size(), "stub.dex", &error)) {
        std::printf("  FAIL AddDexFile: %s\n=== FAILED ===\n", error.c_str());
        return 1;
    }

    kudroid::kuart::Interpreter interp(&linker);
    kudroid::kuart::DexJniEnv jni(&linker, &interp);
    interp.set_jni_env(&jni);
    kudroid::kuart::DexReflect reflect(&linker, &interp, &jni);

    // ── which packages get stubbed ──
    // Boot-classpath packages are KuDroid's responsibility to ship, so a missing
    // one is stubbed and logged for scripts/generate_framework_stubs.py.
    struct Case {
        const char* descriptor;
        bool expect_stub;
        const char* why;
    };
    const Case cases[] = {
        {"Landroid/nowhere/Ghost;", true, "android.* is boot classpath"},
        {"Landroidx/nowhere/Ghost;", true, "androidx.* is boot classpath"},
        {"Ljava/nowhere/Ghost;", true, "java.* is boot classpath"},
        {"Ljavax/nowhere/Ghost;", true, "javax.* is boot classpath"},
        {"Ldalvik/nowhere/Ghost;", true, "dalvik.* is boot classpath"},
        {"Lsun/nowhere/Ghost;", true, "sun.* is boot classpath"},
        {"Llibcore/nowhere/Ghost;", true, "libcore.* is boot classpath"},
        {"Lcom/android/nowhere/Ghost;", true, "com.android.* is boot classpath"},
        {"Lorg/json/Ghost;", true, "org.json is boot classpath"},
        {"Lorg/xml/sax/Ghost;", true, "org.xml.sax is boot classpath"},

        // App packages: a missing class here means the name is wrong or a DEX is
        // absent. Both must surface, so no stub.
        {"Lcom/mojang/minecraftpe/Main;", false, "app package: the Main-guess bug"},
        {"Lcom/unity3d/player/Ghost;", false, "app package, not framework"},
        {"Lcom/facebook/react/Ghost;", false, "app package, not framework"},
        {"Lcom/google/android/gms/Ghost;", false, "third-party lib, ships in the APK"},
        {"Lkotlin/jvm/internal/Ghost;", false, "kotlin stdlib ships in the APK"},
        {"Lorg/greenrobot/Ghost;", false, "arbitrary org.* is not boot classpath"},
        {"Lcom/foo/Bar;", false, "unknown package"},
    };

    for (const Case& c : cases) {
        kudroid::kuart::DexClass* k = linker.FindClass(c.descriptor);
        if (c.expect_stub) {
            Check(k != nullptr && k->is_stub,
                  std::string("stubbed: ") + c.descriptor + " (" + c.why + ")");
        } else {
            Check(k == nullptr,
                  std::string("NOT stubbed: ") + c.descriptor + " (" + c.why + ")");
        }
    }

    // A stub must still look like a plain Object subclass so that merely holding a
    // reference to one does not break; only *using* it fails.
    {
        kudroid::kuart::DexClass* ghost = linker.FindClass("Landroid/nowhere/Ghost;");
        Check(ghost != nullptr && ghost->superclass != nullptr &&
                  std::strcmp(ghost->superclass->descriptor, "Ljava/lang/Object;") == 0,
              "stub's superclass is java.lang.Object");
        Check(ghost != nullptr && ghost->dex_file == nullptr,
              "stub has no dex_file (nothing to load members from)");
    }

    // Real classes must not be flagged.
    {
        kudroid::kuart::DexClass* t = linker.FindClass("LT;");
        Check(t != nullptr && !t->is_stub, "a class defined in the DEX is not a stub");
        kudroid::kuart::DexClass* arr = linker.FindClass("[I");
        Check(arr != nullptr && !arr->is_stub, "array class is not a stub");
        kudroid::kuart::DexClass* prim = linker.FindClass("I");
        Check(prim != nullptr && !prim->is_stub, "primitive class is not a stub");
    }

    // ── using a stub must fail loudly ──

    // This is the ClassCastException chain in miniature: forName reporting success
    // for a class with no members is what let a bad Activity candidate through.
    Check(reflect.ForName("android.nowhere.Ghost") == nullptr,
          "DexReflect::ForName reports a stub as absent");
    Check(reflect.ForName("com.mojang.minecraftpe.Main") == nullptr,
          "DexReflect::ForName on an app class that does not exist -> null");

    {
        kudroid::kuart::DexClass* ghost = linker.FindClass("Landroid/nowhere/Ghost;");
        Check(reflect.NewInstance(ghost) == nullptr,
              "DexReflect::NewInstance refuses a stub");
    }

    // new-instance in bytecode: NoClassDefFoundError, not a hollow object.
    {
        kudroid::kuart::DexClass* t = linker.FindClass("LT;");
        kudroid::kuart::DexMethod* m =
            t != nullptr ? t->FindDirectMethod("makeStub", "()Ljava/lang/Object;") : nullptr;
        Check(m != nullptr, "found makeStub");
        if (m != nullptr) {
            interp.ClearPendingException();
            const DexValue r = interp.Execute(m, nullptr, 0);
            Check(interp.HasPendingException(),
                  "new-instance on a stub throws instead of allocating");
            Check(interp.last_error().find("NoClassDefFoundError") != std::string::npos,
                  "the exception is NoClassDefFoundError");
            Check(r.l == nullptr, "no object leaks out of the failed new-instance");
            interp.ClearPendingException();
        }
    }

    if (g_failures == 0) {
        std::printf("=== KuART auto-stub test PASSED ===\n");
        return 0;
    }
    std::printf("=== KuART auto-stub test FAILED (%d errors) ===\n", g_failures);
    return 1;
}
