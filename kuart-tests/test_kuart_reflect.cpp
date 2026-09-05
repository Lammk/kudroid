// Host test for DexReflect + object java.lang.Class (const-class).
//
// Mirrors the real ActivityThread path: Class.forName(dotted name) -> newInstance()
// -> getMethod() -> invoke(), including dispatch through a subclass.
#include "kudroid/kuart/DexJniEnv.h"
#include "kudroid/kuart/DexReflect.h"

#include <cstdio>
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
void Op21s(std::vector<uint16_t>* code, uint8_t op, uint8_t a, int16_t v) {
    code->push_back(static_cast<uint16_t>(op | (a << 8)));
    code->push_back(static_cast<uint16_t>(v));
}
void Op22c(std::vector<uint16_t>* code, uint8_t op, uint8_t a, uint8_t b, uint16_t idx) {
    code->push_back(static_cast<uint16_t>(op | (a << 8) | (b << 12)));
    code->push_back(idx);
}

constexpr uint8_t kOpConstClass = 0x1c;
constexpr uint8_t kOpConst16 = 0x13;
constexpr uint8_t kOpReturnVoid = 0x0e;
constexpr uint8_t kOpReturn = 0x0f;
constexpr uint8_t kOpReturnObject = 0x11;
constexpr uint8_t kOpIget = 0x52;
constexpr uint8_t kOpIput = 0x59;
constexpr uint8_t kOpSput = 0x67;

constexpr uint32_t kAccPublicStatic = 0x1 | 0x8;
constexpr uint32_t kAccStaticCtor = 0x8 | 0x10000;

struct Specs {
    FieldSpec base_v{"v", "I", 0x1};
    FieldSpec inited{"inited", "I", 0x9};

    MethodSpec object_ctor;
    MethodSpec base_ctor;
    MethodSpec base_clinit;
    MethodSpec base_get;      // virtual: return v
    MethodSpec base_set;      // virtual: v = param
    MethodSpec base_twice;    // virtual: return v*2 (Sub overrides to v*3)
    MethodSpec base_static;   // static: return 5
    MethodSpec sub_ctor;
    MethodSpec sub_twice;

    MethodSpec iface_ctor;    // Abs unused but needed for ClassSpec

    MethodSpec my_class;      // static: const-class LBase; return object

    Specs() {
        object_ctor.name = "<init>";
        object_ctor.access_flags = 0x10001;
        base_ctor.name = "<init>";
        base_ctor.access_flags = 0x10001;
        sub_ctor.name = "<init>";
        sub_ctor.access_flags = 0x10001;
        iface_ctor.name = "<init>";
        iface_ctor.access_flags = 0x10001;

        base_clinit.name = "<clinit>";
        base_clinit.access_flags = kAccStaticCtor;

        base_get.name = "get";
        base_get.return_type = "I";
        base_set.name = "set";
        base_set.params = {"I"};
        base_twice.name = "twice";
        base_twice.return_type = "I";
        sub_twice.name = "twice";
        sub_twice.return_type = "I";

        base_static.name = "five";
        base_static.return_type = "I";
        base_static.access_flags = kAccPublicStatic;

        my_class.name = "myClass";
        my_class.return_type = "Ljava/lang/Class;";
        my_class.access_flags = kAccPublicStatic;
    }
};

std::vector<ClassSpec> BuildClasses(const Specs& s) {
    ClassSpec object;
    object.descriptor = "Ljava/lang/Object;";
    object.superclass = "";
    object.direct_methods = {s.object_ctor};

    ClassSpec string;
    string.descriptor = "Ljava/lang/String;";

    ClassSpec class_class;
    class_class.descriptor = "Ljava/lang/Class;";

    ClassSpec base;
    base.descriptor = "Lcom/foo/Base;";
    base.instance_fields = {s.base_v};
    base.static_fields = {s.inited};
    base.direct_methods = {s.base_ctor, s.base_clinit, s.base_static};
    base.virtual_methods = {s.base_get, s.base_set, s.base_twice};

    ClassSpec sub;
    sub.descriptor = "Lcom/foo/Sub;";
    sub.superclass = "Lcom/foo/Base;";
    sub.direct_methods = {s.sub_ctor};
    sub.virtual_methods = {s.sub_twice};

    ClassSpec iface;
    iface.descriptor = "Lcom/foo/Iface;";
    iface.access_flags = 0x1 | 0x200;  // ACC_PUBLIC | ACC_INTERFACE

    ClassSpec abs;
    abs.descriptor = "Lcom/foo/Abs;";
    abs.access_flags = 0x1 | 0x400;  // ACC_PUBLIC | ACC_ABSTRACT
    abs.direct_methods = {s.iface_ctor};

    ClassSpec r;
    r.descriptor = "LR;";
    r.extra_types = {"Lcom/foo/Base;"};
    r.direct_methods = {s.my_class};

    return {object, string, class_class, base, sub, iface, abs, r};
}

}  // namespace

int main() {
std::printf("=== KuART b c 5: reflection + java.lang.Class ===\n");

    Specs probe;
    DexBuilder index_builder;
    index_builder.Build(BuildClasses(probe));

    const uint16_t kTypeBase =
        static_cast<uint16_t>(index_builder.TypeIndexOf("Lcom/foo/Base;"));
    const uint16_t kFieldV =
        static_cast<uint16_t>(index_builder.FieldIndexOf("Lcom/foo/Base;", probe.base_v));
    const uint16_t kFieldInited =
        static_cast<uint16_t>(index_builder.FieldIndexOf("Lcom/foo/Base;", probe.inited));

    Specs s;
    s.object_ctor.code = {Op11x(kOpReturnVoid, 0)};
    s.object_ctor.registers_size = 1;
    s.object_ctor.ins_size = 1;

    // Base.<init>(): v = 7 to tell objects apart by constructor.
    {
        std::vector<uint16_t> c;
        Op21s(&c, kOpConst16, 0, 7);
        Op22c(&c, kOpIput, 0, 1, kFieldV);
        c.push_back(Op11x(kOpReturnVoid, 0));
        s.base_ctor.code = c;
        s.base_ctor.registers_size = 2;
        s.base_ctor.ins_size = 1;
    }
    s.sub_ctor.code = {Op11x(kOpReturnVoid, 0)};
    s.sub_ctor.registers_size = 1;
    s.sub_ctor.ins_size = 1;
    s.iface_ctor.code = {Op11x(kOpReturnVoid, 0)};
    s.iface_ctor.registers_size = 1;
    s.iface_ctor.ins_size = 1;

    // Base.<clinit>(): inited = 1
    {
        std::vector<uint16_t> c;
        Op21s(&c, kOpConst16, 0, 1);
        Op21c(&c, kOpSput, 0, kFieldInited);
        c.push_back(Op11x(kOpReturnVoid, 0));
        s.base_clinit.code = c;
        s.base_clinit.registers_size = 1;
    }
    // Base.get(): return this.v
    {
        std::vector<uint16_t> c;
        Op22c(&c, kOpIget, 0, 1, kFieldV);
        c.push_back(Op11x(kOpReturn, 0));
        s.base_get.code = c;
        s.base_get.registers_size = 2;
        s.base_get.ins_size = 1;
    }
    // Base.set(int n): this.v = n   (v1 = this, v2 = n)
    {
        std::vector<uint16_t> c;
        Op22c(&c, kOpIput, 2, 1, kFieldV);
        c.push_back(Op11x(kOpReturnVoid, 0));
        s.base_set.code = c;
        s.base_set.registers_size = 3;
        s.base_set.ins_size = 2;
    }
    // Base.twice(): return 20
    {
        std::vector<uint16_t> c;
        Op21s(&c, kOpConst16, 0, 20);
        c.push_back(Op11x(kOpReturn, 0));
        s.base_twice.code = c;
        s.base_twice.registers_size = 2;
        s.base_twice.ins_size = 1;
    }
    // Sub.twice(): return 30  — override
    {
        std::vector<uint16_t> c;
        Op21s(&c, kOpConst16, 0, 30);
        c.push_back(Op11x(kOpReturn, 0));
        s.sub_twice.code = c;
        s.sub_twice.registers_size = 2;
        s.sub_twice.ins_size = 1;
    }
    // Base.five(): return 5
    {
        std::vector<uint16_t> c;
        Op21s(&c, kOpConst16, 0, 5);
        c.push_back(Op11x(kOpReturn, 0));
        s.base_static.code = c;
        s.base_static.registers_size = 1;
    }
    // R.myClass(): return Base.class
    {
        std::vector<uint16_t> c;
        Op21c(&c, kOpConstClass, 0, kTypeBase);
        c.push_back(Op11x(kOpReturnObject, 0));
        s.my_class.code = c;
        s.my_class.registers_size = 1;
    }

    DexBuilder builder;
    const std::vector<uint8_t> dex = builder.Build(BuildClasses(s));
    std::printf("DEX synthetic: %zu bytes\n", dex.size());
    Check(builder.TypeIndexOf("Lcom/foo/Base;") == kTypeBase,
"index  n  nh gi a hai l t build");

    kudroid::kuart::DexClassLinker linker;
    std::string error;
    if (!linker.AddDexFile(dex.data(), dex.size(), "test.dex", &error)) {
        std::printf("  FAIL AddDexFile: %s\n=== FAILED ===\n", error.c_str());
        return 1;
    }

    kudroid::kuart::Interpreter interp(&linker);
    kudroid::kuart::DexJniEnv jni(&linker, &interp);
    interp.set_jni_env(&jni);
    kudroid::kuart::DexReflect reflect(&linker, &interp, &jni);

    using kudroid::kuart::DexReflect;

    // name conversions
    Check(DexReflect::DottedToDescriptor("com.foo.Base") == "Lcom/foo/Base;",
          "DottedToDescriptor");
    Check(DexReflect::DottedToDescriptor("Lcom/foo/Base;") == "Lcom/foo/Base;",
"descriptor truy n v o kh ng b  b c hai l n");
Check(DexReflect::DottedToDescriptor("[I") == "[I", "t n m ng gi  nguy n");
    Check(DexReflect::DescriptorToDotted("Lcom/foo/Base;") == "com.foo.Base",
          "DescriptorToDotted");
    Check(DexReflect::DescriptorToDotted("[Ljava/lang/String;") == "[Ljava.lang.String;",
"m ng gi  descriptor, ch   i d u ch m");

    // ── Class.forName ──
    {
        kudroid::kuart::DexClass* base = reflect.ForName("com.foo.Base");
Check(base != nullptr, "forName v i t n c  d u ch m");
        Check(base != nullptr &&
                  base->status == kudroid::kuart::DexClass::Status::kInitialized,
"forName ch y <clinit> (initialize = true)");
        if (base != nullptr) {
            kudroid::kuart::DexField* f = base->FindStaticField("inited", "I");
            Check(f != nullptr && base->static_values[f->offset_or_slot].i == 1,
"<clinit>    t inited = 1");
        }
Check(reflect.ForName("com.foo.KhongCo") == nullptr, "forName class kh ng c    null");
Check(reflect.GetName(base) == "com.foo.Base", "getName tr  t n c  d u ch m");
    }

    // ── newInstance + invoke ──
    {
        kudroid::kuart::DexClass* base = reflect.ForName("com.foo.Base");
        kudroid::kuart::DexObject* obj = reflect.NewInstance(base);
Check(obj != nullptr, "newInstance t o  c object");
Check(obj != nullptr && obj->clazz == base, "object c  class  ng");

        kudroid::kuart::DexMethod* get = reflect.FindMethod(base, "get", "()I");
        Check(get != nullptr, "getMethod(get)");
        const DexValue self = DexValue::Ref(obj);
        Check(reflect.Invoke(get, obj, &self, 1).i == 7,
"invoke th y gi  tr  do constructor  t");

        kudroid::kuart::DexMethod* set = reflect.FindMethod(base, "set", "(I)V");
        Check(set != nullptr, "getMethod(set)");
        const DexValue set_args[2] = {DexValue::Ref(obj), DexValue::Int(99)};
        reflect.Invoke(set, obj, set_args, 2);
Check(reflect.Invoke(get, obj, &self, 1).i == 99, "invoke method c  tham s ");

        kudroid::kuart::DexMethod* five = reflect.FindMethod(base, "five", "()I");
        Check(five != nullptr, "getMethod(five) static");
        Check(reflect.Invoke(five, nullptr, nullptr, 0).i == 5, "invoke method static");

        Check(reflect.FindMethod(base, "khongCo", "()V") == nullptr,
"getMethod method kh ng c    null");
    }

    // correct dispatch via reflection
    {
        kudroid::kuart::DexClass* base = reflect.ForName("com.foo.Base");
        kudroid::kuart::DexClass* sub = reflect.ForName("com.foo.Sub");
        kudroid::kuart::DexMethod* twice = reflect.FindMethod(base, "twice", "()I");

        kudroid::kuart::DexObject* base_obj = reflect.NewInstance(base);
        kudroid::kuart::DexObject* sub_obj = reflect.NewInstance(sub);
        const DexValue base_self = DexValue::Ref(base_obj);
        const DexValue sub_self = DexValue::Ref(sub_obj);

        Check(reflect.Invoke(twice, base_obj, &base_self, 1).i == 20,
"invoke tr n Base g i Base.twice");
        Check(reflect.Invoke(twice, sub_obj, &sub_self, 1).i == 30,
"C NG Method nh ng receiver Sub g i Sub.twice (dispatch  ng)");
    }

    // cannot instantiate
    {
        Check(reflect.NewInstance(reflect.ForName("com.foo.Iface")) == nullptr,
"newInstance tr n interface   null");
        Check(reflect.NewInstance(reflect.ForName("com.foo.Abs")) == nullptr,
"newInstance tr n abstract class   null");
    }

    // java.lang.Class object from const-class
    {
        kudroid::kuart::DexClass* r = linker.FindClass("LR;");
        kudroid::kuart::DexMethod* m =
            r != nullptr ? r->FindDirectMethod("myClass", "()Ljava/lang/Class;") : nullptr;
Check(m != nullptr, "t m  c R.myClass");
        interp.ClearPendingException();
        const DexValue cls = interp.Execute(m, nullptr, 0);
        Check(!interp.HasPendingException() && cls.l != nullptr,
"const-class tr  object kh c null");
        Check(linker.ClassFromObject(cls.l) == reflect.ForName("com.foo.Base"),
"object Class tr   ng DexClass");
        Check(cls.l->clazz == linker.FindClass("Ljava/lang/Class;"),
"object Class c  clazz = java.lang.Class");

        const DexValue again = interp.Execute(m, nullptr, 0);
        Check(again.l == cls.l, "Base.class == Base.class (cache class_object)");

        kudroid::kuart::DexObject* plain =
            linker.AllocObject(linker.FindClass("Lcom/foo/Base;"));
        Check(linker.ClassFromObject(plain) == nullptr,
"object th ng kh ng b  nh n nh m l  Class");
    }

std::printf("=== %s (%d error) ===\n", g_failures == 0 ? "PASSED" : "FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}
