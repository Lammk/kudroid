// test_kuart_method_handles.cpp — primitive class literals and java.lang.invoke.
// Null TYPE fields broke every reflective lookup mentioning a primitive; the invoke
// half pins unreflect vs unreflectSpecial dispatch on proxies.
#include "kudroid/framework_dex_bytes.h"
#include "kudroid/kuart/DexClassLinker.h"
#include "kudroid/kuart/DexJniEnv.h"
#include "kudroid/kuart/DexObject.h"
#include "kudroid/kuart/DexString.h"
#include "kudroid/kuart/Interpreter.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "dex_builder.h"

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    std::printf("%s %s\n", ok ? "  OK  " : "  FAIL", what.c_str());
    if (!ok) ++g_failures;
}

using namespace dexbuild;
using kudroid::kuart::DexArray;
using kudroid::kuart::DexClass;
using kudroid::kuart::DexClassLinker;
using kudroid::kuart::DexMethod;
using kudroid::kuart::DexObject;
using kudroid::kuart::DexString;
using kudroid::kuart::DexValue;
using kudroid::kuart::Interpreter;

uint16_t Op11x(uint8_t op, uint8_t a) { return static_cast<uint16_t>(op | (a << 8)); }
void Op21s(std::vector<uint16_t>* code, uint8_t op, uint8_t a, int16_t v) {
    code->push_back(static_cast<uint16_t>(op | (a << 8)));
    code->push_back(static_cast<uint16_t>(v));
}
void Op35c(std::vector<uint16_t>* code, uint8_t op, uint16_t idx,
           const std::vector<uint8_t>& regs) {
    code->push_back(static_cast<uint16_t>(op | (regs.size() << 12)));
    code->push_back(idx);
    uint16_t packed = 0;
    for (size_t i = 0; i < regs.size() && i < 4; ++i) {
        packed |= static_cast<uint16_t>((regs[i] & 0xF) << (i * 4));
    }
    code->push_back(packed);
}

constexpr uint8_t kOpConst16 = 0x13;
constexpr uint8_t kOpMulInt2Addr = 0xb2;
constexpr uint8_t kOpReturnVoid = 0x0e;
constexpr uint8_t kOpReturn = 0x0f;
constexpr uint8_t kOpReturnObject = 0x11;
constexpr uint8_t kOpMoveResultObject = 0x0c;
constexpr uint8_t kOpInvokeStatic = 0x71;

constexpr uint32_t kAccPublic = 0x1;
constexpr uint32_t kAccStatic = 0x8;
constexpr uint32_t kAccPublicStatic = kAccPublic | kAccStatic;
constexpr uint32_t kAccInterface = 0x200;
constexpr uint32_t kAccAbstract = 0x400;
constexpr uint32_t kAccConstructor = 0x10000;

// The class graph under test.
//
//   interface Greeter {
//       int describe();                              // abstract
//       default int twice(int x) { return x * 2; }   // DEFAULT, with a real body
//   }
//   class Override implements Greeter {
//       public int twice(int x) { return x * 100; }   // overrides the default
//       public int describe() { return 5; }
//   }
//   class H implements InvocationHandler { Object invoke(...) { return Integer.valueOf(7); } }
//
// Override.twice existing is what makes the `special` flag testable without ambiguity:
// unreflect(Greeter.twice) on an Override must dispatch virtually and reach Override's body
// (100*x), while unreflectSpecial(Greeter.twice) must run Greeter's own body (2*x). One
// object, one Method, two handles, two different answers — a handle that silently ignored
// the flag would return the same number for both.
struct Specs {
    MethodSpec greeter_describe;
    MethodSpec greeter_twice;
    MethodSpec override_ctor;
    MethodSpec override_twice;
    MethodSpec override_describe;
    MethodSpec handler_ctor;
    MethodSpec handler_invoke;

    MethodRefSpec integer_value_of{"Ljava/lang/Integer;", "valueOf",
                                   "Ljava/lang/Integer;", {"I"}};

    Specs() {
        greeter_describe.name = "describe";
        greeter_describe.return_type = "I";
        greeter_describe.access_flags = kAccPublic | kAccAbstract;

        greeter_twice.name = "twice";
        greeter_twice.return_type = "I";
        greeter_twice.params = {"I"};
        // NOT abstract: a default method has a body, and that flag combination is exactly
        // what Method.isDefault() reports on.
        greeter_twice.access_flags = kAccPublic;

        override_ctor.name = "<init>";
        override_ctor.access_flags = kAccPublic | kAccConstructor;

        override_twice.name = "twice";
        override_twice.return_type = "I";
        override_twice.params = {"I"};
        override_twice.access_flags = kAccPublic;

        override_describe.name = "describe";
        override_describe.return_type = "I";
        override_describe.access_flags = kAccPublic;

        handler_ctor.name = "<init>";
        handler_ctor.access_flags = kAccPublic | kAccConstructor;

        handler_invoke.name = "invoke";
        handler_invoke.return_type = "Ljava/lang/Object;";
        handler_invoke.params = {"Ljava/lang/Object;", "Ljava/lang/reflect/Method;",
                                 "[Ljava/lang/Object;"};
        handler_invoke.access_flags = kAccPublic;
    }
};

std::vector<ClassSpec> BuildClasses(const Specs& s) {
    ClassSpec greeter;
    greeter.descriptor = "Lcom/foo/Greeter;";
    greeter.access_flags = kAccPublic | kAccInterface | kAccAbstract;
    greeter.virtual_methods = {s.greeter_describe, s.greeter_twice};

    ClassSpec over;
    over.descriptor = "Lcom/foo/Override;";
    over.interfaces = {"Lcom/foo/Greeter;"};
    over.direct_methods = {s.override_ctor};
    over.virtual_methods = {s.override_twice, s.override_describe};

    ClassSpec handler;
    handler.descriptor = "Lcom/foo/H;";
    handler.interfaces = {"Ljava/lang/reflect/InvocationHandler;"};
    handler.direct_methods = {s.handler_ctor};
    handler.virtual_methods = {s.handler_invoke};
    handler.extra_method_refs = {s.integer_value_of};

    return {greeter, over, handler};
}

DexClassLinker* g_linker = nullptr;

DexObject* NewClassArray(const std::vector<DexClass*>& classes) {
    DexClass* array_class = g_linker->FindClass("[Ljava/lang/Class;");
    if (array_class == nullptr) return nullptr;
    DexArray* arr = g_linker->AllocArray(array_class, static_cast<int32_t>(classes.size()));
    if (arr == nullptr) return nullptr;
    auto** data = reinterpret_cast<DexObject**>(arr + 1);
    for (size_t i = 0; i < classes.size(); ++i) {
        data[i] = reinterpret_cast<DexObject*>(g_linker->GetClassObject(classes[i]));
    }
    return reinterpret_cast<DexObject*>(arr);
}

DexObject* NewObjectArray(const std::vector<DexObject*>& items) {
    DexClass* array_class = g_linker->FindClass("[Ljava/lang/Object;");
    if (array_class == nullptr) return nullptr;
    DexArray* arr = g_linker->AllocArray(array_class, static_cast<int32_t>(items.size()));
    if (arr == nullptr) return nullptr;
    auto** data = reinterpret_cast<DexObject**>(arr + 1);
    for (size_t i = 0; i < items.size(); ++i) data[i] = items[i];
    return reinterpret_cast<DexObject*>(arr);
}

// The Class object behind a static TYPE field, or null.
DexClass* ReadTypeField(Interpreter* interp, DexClassLinker* linker, const char* box_desc) {
    DexClass* box = linker->FindClass(box_desc);
    if (box == nullptr) return nullptr;
    interp->EnsureInitialized(box);
    interp->ClearPendingException();
    kudroid::kuart::DexField* f = box->FindStaticField("TYPE", "Ljava/lang/Class;");
    if (f == nullptr || f->offset_or_slot >= box->static_values.size()) return nullptr;
    return linker->ClassFromObject(box->static_values[f->offset_or_slot].l);
}

// handle.bindTo(receiver).invokeWithArguments(new Object[]{ Integer.valueOf(arg) }), unboxed.
//
// Every step goes through the Java API rather than the C++ helpers, because the defect being
// pinned is only observable through a real call: a handle that dispatches the wrong way still
// returns a plausible Integer.
//
// Returns -1 on any failure, which is distinguishable from every expected result here.
int CallHandle(Interpreter* interp, DexClassLinker* linker, DexMethod* bind_to,
               DexMethod* invoke_with, DexObject* handle, DexObject* receiver, int32_t arg) {
    if (handle == nullptr || bind_to == nullptr || invoke_with == nullptr) return -1;

    DexValue ba[2] = {DexValue::Ref(handle), DexValue::Ref(receiver)};
    interp->ClearPendingException();
    const DexValue bound = interp->Execute(bind_to, ba, 2);
    if (interp->HasPendingException() || bound.l == nullptr) {
        interp->ClearPendingException();
        return -1;
    }

    DexClass* integer = linker->FindClass("Ljava/lang/Integer;");
    DexMethod* value_of =
        integer != nullptr ? integer->FindDirectMethod("valueOf", "(I)Ljava/lang/Integer;")
                           : nullptr;
    if (value_of == nullptr) return -1;
    DexValue boxed_in = DexValue::Int(arg);
    const DexValue boxed = interp->Execute(value_of, &boxed_in, 1);
    interp->ClearPendingException();

    DexValue ia[2] = {bound, DexValue::Ref(NewObjectArray({boxed.l}))};
    interp->ClearPendingException();
    const DexValue got = interp->Execute(invoke_with, ia, 2);
    if (interp->HasPendingException()) {
        interp->ClearPendingException();
        return -1;
    }
    if (got.l == nullptr) return -1;

    DexClass* box = linker->ClassOfObject(got.l);
    DexMethod* int_value =
        box != nullptr ? box->FindVirtualMethod("intValue", "()I") : nullptr;
    if (int_value == nullptr) return -1;
    DexValue self = got;
    const DexValue v = interp->Execute(int_value, &self, 1);
    interp->ClearPendingException();
    return v.i;
}

}  // namespace

int main() {
    std::printf("=== KuART primitive class literals + java.lang.invoke ===\n");

    Specs probe;
    DexBuilder index_builder;
    index_builder.Build(BuildClasses(probe));
    const uint16_t kIntegerValueOf =
        static_cast<uint16_t>(index_builder.MethodRefIndexOf(probe.integer_value_of));

    Specs s;
    // Greeter.twice(int x) { return x * 2; }  — v0 result, p0=this(v1), p1=x(v2)
    {
        std::vector<uint16_t> c;
        Op21s(&c, kOpConst16, 0, 2);
        c.push_back(static_cast<uint16_t>(kOpMulInt2Addr | (0 << 8) | (2 << 12)));
        c.push_back(Op11x(kOpReturn, 0));
        s.greeter_twice.code = c;
        s.greeter_twice.registers_size = 3;
        s.greeter_twice.ins_size = 2;
    }
    s.override_ctor.code = {Op11x(kOpReturnVoid, 0)};
    s.override_ctor.registers_size = 1;
    s.override_ctor.ins_size = 1;
    // Override.twice(int x) { return x * 100; } — a different multiplier so the two
    // dispatch modes cannot be confused.
    {
        std::vector<uint16_t> c;
        Op21s(&c, kOpConst16, 0, 100);
        c.push_back(static_cast<uint16_t>(kOpMulInt2Addr | (0 << 8) | (2 << 12)));
        c.push_back(Op11x(kOpReturn, 0));
        s.override_twice.code = c;
        s.override_twice.registers_size = 3;
        s.override_twice.ins_size = 2;
    }
    {
        std::vector<uint16_t> c;
        Op21s(&c, kOpConst16, 0, 5);
        c.push_back(Op11x(kOpReturn, 0));
        s.override_describe.code = c;
        s.override_describe.registers_size = 2;
        s.override_describe.ins_size = 1;
    }
    s.handler_ctor.code = {Op11x(kOpReturnVoid, 0)};
    s.handler_ctor.registers_size = 1;
    s.handler_ctor.ins_size = 1;
    // H.invoke(...) { return Integer.valueOf(7); }  — v0 scratch, p0..p3 = v1..v4
    {
        std::vector<uint16_t> c;
        Op21s(&c, kOpConst16, 0, 7);
        Op35c(&c, kOpInvokeStatic, kIntegerValueOf, {0});
        c.push_back(Op11x(kOpMoveResultObject, 0));
        c.push_back(Op11x(kOpReturnObject, 0));
        s.handler_invoke.code = c;
        s.handler_invoke.registers_size = 5;
        s.handler_invoke.ins_size = 4;
        s.handler_invoke.outs_size = 1;
    }

    DexBuilder builder;
    const std::vector<uint8_t> dex = builder.Build(BuildClasses(s));

    DexClassLinker linker;
    std::string error;
    if (!linker.AddDexFile(g_framework_dex_bytes, g_framework_dex_size, "framework.dex",
                           &error)) {
        std::printf("  FAIL AddDexFile(framework.dex): %s\n=== FAILED ===\n", error.c_str());
        return 1;
    }
    if (!linker.AddDexFile(dex.data(), dex.size(), "test.dex", &error)) {
        std::printf("  FAIL AddDexFile(test.dex): %s\n=== FAILED ===\n", error.c_str());
        return 1;
    }
    g_linker = &linker;

    Interpreter interp(&linker);
    kudroid::kuart::DexJniEnv jni(&linker, &interp);
    interp.set_jni_env(&jni);

    // ── the framework classes must be real, not auto-stubs ──
    //
    // A stub has no methods and no fields, so every check below would "pass" against one
    // while doing nothing at all. Establish this first.
    {
        std::printf("[framework] the classes exist and are not auto-stubs\n");
        const char* required[] = {
            "Ljava/lang/Void;",
            "Ljava/lang/invoke/MethodType;",
            "Ljava/lang/invoke/MethodHandle;",
            "Ljava/lang/invoke/MethodHandle$Direct;",
            "Ljava/lang/invoke/MethodHandles;",
            "Ljava/lang/invoke/MethodHandles$Lookup;",
        };
        for (const char* desc : required) {
            DexClass* k = linker.FindClass(desc);
            Check(k != nullptr && !k->is_stub, std::string(desc) + " is a real class");
        }
    }

    // ── primitive class literals resolve, and each names its own type ──
    //
    // Non-null is not enough: one shared placeholder would satisfy that and still break
    // every signature comparison, so each TYPE must be the class for its own descriptor.
    {
        std::printf("[primitive] Integer.TYPE and friends are the real primitive classes\n");
        struct Case { const char* box; const char* descriptor; const char* java_name; };
        const Case cases[] = {
            {"Ljava/lang/Integer;",   "I", "int"},
            {"Ljava/lang/Long;",      "J", "long"},
            {"Ljava/lang/Short;",     "S", "short"},
            {"Ljava/lang/Byte;",      "B", "byte"},
            {"Ljava/lang/Character;", "C", "char"},
            {"Ljava/lang/Float;",     "F", "float"},
            {"Ljava/lang/Double;",    "D", "double"},
            {"Ljava/lang/Boolean;",   "Z", "boolean"},
            {"Ljava/lang/Void;",      "V", "void"},
        };
        for (const Case& c : cases) {
            DexClass* named = ReadTypeField(&interp, &linker, c.box);
            const bool right = named != nullptr && named->is_primitive &&
                               named->descriptor != nullptr &&
                               std::strcmp(named->descriptor, c.descriptor) == 0;
            Check(right, std::string(c.java_name) + ".class is the primitive class for \"" +
                             c.descriptor + "\"");
        }
    }

    // ── the same Class object every time ──
    //
    // Reference identity is what getMethod compares with. A fresh Class per read would pass
    // every check above and still fail every lookup.
    {
        std::printf("[primitive] repeated reads give the identical object\n");
        DexClass* prim_int = linker.FindClass("I");
        Check(prim_int != nullptr, "the primitive class for \"I\" exists");
        if (prim_int != nullptr) {
            DexObject* a = reinterpret_cast<DexObject*>(linker.GetClassObject(prim_int));
            DexObject* b = reinterpret_cast<DexObject*>(linker.GetClassObject(prim_int));
            Check(a != nullptr && a == b, "GetClassObject(int) is stable across calls");
            DexClass* box = linker.FindClass("Ljava/lang/Integer;");
            if (box != nullptr) {
                interp.EnsureInitialized(box);
                interp.ClearPendingException();
                kudroid::kuart::DexField* f =
                    box->FindStaticField("TYPE", "Ljava/lang/Class;");
                if (f != nullptr && f->offset_or_slot < box->static_values.size()) {
                    Check(box->static_values[f->offset_or_slot].l == a,
                          "Integer.TYPE is that same object — identity comparison works");
                }
            }
        }
    }

    // ── a reflective lookup whose signature mentions a primitive now matches ──
    //
    // This is the defect's real shape and the reason it mattered beyond class literals:
    // getDeclaredConstructor(Class.class, int.class) is how a library obtains a Lookup with
    // private access, and with int.class == null the parameter comparison could never
    // succeed. Run it against MethodHandles$Lookup, the exact constructor such code wants.
    {
        std::printf("[primitive] getDeclaredConstructor matches on (Class, int)\n");
        DexClass* cls = linker.FindClass("Ljava/lang/Class;");
        DexClass* lookup_class =
            linker.FindClass("Ljava/lang/invoke/MethodHandles$Lookup;");
        DexClass* prim_int = linker.FindClass("I");
        DexMethod* get_ctor =
            cls != nullptr
                ? cls->FindVirtualMethod("getDeclaredConstructor",
                                         "([Ljava/lang/Class;)Ljava/lang/reflect/Constructor;")
                : nullptr;
        Check(get_ctor != nullptr, "Class.getDeclaredConstructor is present");
        if (get_ctor != nullptr && lookup_class != nullptr && cls != nullptr &&
            prim_int != nullptr) {
            DexValue a[2];
            a[0] = DexValue::Ref(reinterpret_cast<DexObject*>(
                linker.GetClassObject(lookup_class)));
            a[1] = DexValue::Ref(NewClassArray({cls, prim_int}));
            interp.ClearPendingException();
            const DexValue ctor = interp.Execute(get_ctor, a, 2);
            Check(!interp.HasPendingException(),
                  std::string("found Lookup(Class,int): ") +
                      interp.DescribePendingException());
            interp.ClearPendingException();
            Check(ctor.l != nullptr, "and it returned a Constructor object");

            // Instantiating it is what the library actually does next, and it is the step
            // that fails if the constructor found is the wrong one.
            if (ctor.l != nullptr) {
                DexClass* ctor_class = linker.ClassOfObject(ctor.l);
                DexMethod* new_instance =
                    ctor_class != nullptr
                        ? ctor_class->FindVirtualMethod(
                              "newInstance", "([Ljava/lang/Object;)Ljava/lang/Object;")
                        : nullptr;
                DexClass* integer = linker.FindClass("Ljava/lang/Integer;");
                DexMethod* value_of =
                    integer != nullptr
                        ? integer->FindDirectMethod("valueOf", "(I)Ljava/lang/Integer;")
                        : nullptr;
                if (new_instance != nullptr && value_of != nullptr) {
                    DexValue boxed_arg = DexValue::Int(0xF);  // all four access modes
                    const DexValue boxed_mode = interp.Execute(value_of, &boxed_arg, 1);
                    interp.ClearPendingException();
                    DexValue ni[2];
                    ni[0] = ctor;
                    ni[1] = DexValue::Ref(NewObjectArray(
                        {reinterpret_cast<DexObject*>(linker.GetClassObject(cls)),
                         boxed_mode.l}));
                    const DexValue made = interp.Execute(new_instance, ni, 2);
                    Check(!interp.HasPendingException(),
                          std::string("newInstance on it threw nothing: ") +
                              interp.DescribePendingException());
                    interp.ClearPendingException();
                    DexClass* made_class = linker.ClassOfObject(made.l);
                    Check(made_class == lookup_class,
                          "and produced a MethodHandles$Lookup");
                }
            }
        }
    }

    // ── MethodType descriptors round-trip ──
    //
    // The descriptor is what the native lookup path matches against, so parse and render
    // must agree. Round-tripping proves both directions at once.
    {
        std::printf("[MethodType] descriptors round-trip through the Java factory\n");
        DexClass* mt = linker.FindClass("Ljava/lang/invoke/MethodType;");
        DexMethod* from =
            mt != nullptr
                ? mt->FindDirectMethod("fromMethodDescriptorString",
                                       "(Ljava/lang/String;Ljava/lang/ClassLoader;)"
                                       "Ljava/lang/invoke/MethodType;")
                : nullptr;
        DexMethod* to = mt != nullptr ? mt->FindVirtualMethod("toMethodDescriptorString",
                                                             "()Ljava/lang/String;")
                                      : nullptr;
        Check(from != nullptr && to != nullptr,
              "fromMethodDescriptorString and toMethodDescriptorString exist");
        if (from != nullptr && to != nullptr) {
            interp.EnsureInitialized(mt);
            const char* inputs[] = {"(I)I", "()V", "(Ljava/lang/String;I)Z",
                                    "([IJ)Ljava/lang/Object;"};
            for (const char* want : inputs) {
                DexValue a[2];
                a[0] = DexValue::Ref(reinterpret_cast<DexObject*>(linker.NewString(want)));
                a[1] = DexValue::Ref(nullptr);
                interp.ClearPendingException();
                const DexValue type_obj = interp.Execute(from, a, 2);
                if (interp.HasPendingException()) {
                    Check(false, std::string("parsed \"") + want + "\"");
                    interp.ClearPendingException();
                    continue;
                }
                DexValue self = type_obj;
                const DexValue str = interp.Execute(to, &self, 1);
                interp.ClearPendingException();
                auto* out = reinterpret_cast<DexString*>(str.l);
                const char* got = out != nullptr && out->utf8 != nullptr ? out->utf8 : "";
                Check(std::strcmp(got, want) == 0,
                      std::string("\"") + want + "\" round-tripped, got \"" + got + "\"");
            }
        }
    }

    // ── Method.isDefault() distinguishes a default from an abstract declaration ──
    {
        std::printf("[isDefault] only an interface method with a body reports true\n");
        DexClass* greeter = linker.FindClass("Lcom/foo/Greeter;");
        DexClass* method_class = linker.FindClass("Ljava/lang/reflect/Method;");
        DexMethod* is_default =
            method_class != nullptr ? method_class->FindVirtualMethod("isDefault", "()Z")
                                    : nullptr;
        Check(is_default != nullptr, "Method.isDefault is present");
        DexClass* cls = linker.FindClass("Ljava/lang/Class;");
        DexMethod* get_methods =
            cls != nullptr ? cls->FindVirtualMethod("getDeclaredMethods",
                                                    "()[Ljava/lang/reflect/Method;")
                           : nullptr;
        if (greeter != nullptr && is_default != nullptr && get_methods != nullptr) {
            DexValue a = DexValue::Ref(
                reinterpret_cast<DexObject*>(linker.GetClassObject(greeter)));
            interp.ClearPendingException();
            const DexValue arr = interp.Execute(get_methods, &a, 1);
            interp.ClearPendingException();
            auto* methods = reinterpret_cast<DexArray*>(arr.l);
            int default_count = 0;
            int abstract_reported_default = 0;
            if (methods != nullptr) {
                auto** items = reinterpret_cast<DexObject**>(methods + 1);
                for (int32_t i = 0; i < methods->length; ++i) {
                    DexValue self = DexValue::Ref(items[i]);
                    const DexValue got = interp.Execute(is_default, &self, 1);
                    interp.ClearPendingException();
                    // Match by name so the assertion does not depend on table order.
                    DexObject* name_obj = nullptr;
                    if (DexClass* mk = linker.ClassOfObject(items[i])) {
                        if (kudroid::kuart::DexField* nf =
                                mk->FindInstanceField("name", "Ljava/lang/String;")) {
                            name_obj = items[i]->GetField<DexObject*>(nf->offset_or_slot);
                        }
                    }
                    auto* nstr = reinterpret_cast<DexString*>(name_obj);
                    const char* mname =
                        nstr != nullptr && nstr->utf8 != nullptr ? nstr->utf8 : "";
                    if (std::strcmp(mname, "twice") == 0 && got.i != 0) ++default_count;
                    if (std::strcmp(mname, "describe") == 0 && got.i != 0) {
                        ++abstract_reported_default;
                    }
                }
            }
            Check(default_count == 1, "Greeter.twice() reports isDefault() == true");
            Check(abstract_reported_default == 0,
                  "Greeter.describe() is abstract, so isDefault() is false");
        }
    }

    // ── unreflect dispatches virtually, unreflectSpecial does not ──
    //
    // The same Method (Greeter.twice) and the same receiver (an Override, which overrides
    // it), through two handles. unreflect must reach Override's body, unreflectSpecial must
    // reach Greeter's own. A handle that ignored the flag would return the same number
    // twice, and every structural check would still pass.
    DexObject* lookup = nullptr;
    DexObject* twice_method = nullptr;
    DexMethod* bind_to = nullptr;
    DexMethod* invoke_with = nullptr;
    {
        std::printf("[handles] unreflect is virtual, unreflectSpecial is not\n");
        DexClass* greeter = linker.FindClass("Lcom/foo/Greeter;");
        DexClass* over = linker.FindClass("Lcom/foo/Override;");
        DexClass* lookup_class =
            linker.FindClass("Ljava/lang/invoke/MethodHandles$Lookup;");
        DexClass* cls = linker.FindClass("Ljava/lang/Class;");
        DexClass* prim_int = linker.FindClass("I");
        DexMethod* lookup_ctor =
            lookup_class != nullptr
                ? lookup_class->FindDirectMethod("<init>", "(Ljava/lang/Class;I)V")
                : nullptr;
        DexMethod* unreflect =
            lookup_class != nullptr
                ? lookup_class->FindVirtualMethod(
                      "unreflect",
                      "(Ljava/lang/reflect/Method;)Ljava/lang/invoke/MethodHandle;")
                : nullptr;
        DexMethod* unreflect_special =
            lookup_class != nullptr
                ? lookup_class->FindVirtualMethod(
                      "unreflectSpecial",
                      "(Ljava/lang/reflect/Method;Ljava/lang/Class;)"
                      "Ljava/lang/invoke/MethodHandle;")
                : nullptr;
        DexMethod* get_method =
            cls != nullptr
                ? cls->FindVirtualMethod(
                      "getMethod",
                      "(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;")
                : nullptr;
        Check(lookup_ctor != nullptr && unreflect != nullptr && unreflect_special != nullptr,
              "Lookup(Class,int), unreflect and unreflectSpecial are all present");

        if (lookup_class != nullptr && lookup_ctor != nullptr && unreflect != nullptr &&
            unreflect_special != nullptr && get_method != nullptr && greeter != nullptr &&
            over != nullptr && prim_int != nullptr) {
            interp.EnsureInitialized(lookup_class);
            lookup = linker.AllocObject(lookup_class);
            DexValue la[3];
            la[0] = DexValue::Ref(lookup);
            la[1] = DexValue::Ref(
                reinterpret_cast<DexObject*>(linker.GetClassObject(greeter)));
            la[2] = DexValue::Int(0xF);
            interp.ClearPendingException();
            interp.Execute(lookup_ctor, la, 3);
            Check(!interp.HasPendingException(), "constructed a Lookup on Greeter");
            interp.ClearPendingException();

            // Obtained the way an app does — and this is the lookup that could not work
            // before, because its parameter list is {int.class}, which used to be {null}.
            DexValue ga[3];
            ga[0] = DexValue::Ref(
                reinterpret_cast<DexObject*>(linker.GetClassObject(greeter)));
            ga[1] = DexValue::Ref(reinterpret_cast<DexObject*>(linker.NewString("twice")));
            ga[2] = DexValue::Ref(NewClassArray({prim_int}));
            interp.ClearPendingException();
            const DexValue m = interp.Execute(get_method, ga, 3);
            Check(!interp.HasPendingException(),
                  std::string("getMethod(\"twice\", int.class) succeeded: ") +
                      interp.DescribePendingException());
            interp.ClearPendingException();
            twice_method = m.l;
            Check(twice_method != nullptr, "and returned a Method");

            DexObject* receiver = linker.AllocObject(over);
            Check(receiver != nullptr, "allocated an Override, which overrides twice()");

            if (twice_method != nullptr && receiver != nullptr) {
                // unreflect -> virtual
                DexValue ua[2] = {DexValue::Ref(lookup), DexValue::Ref(twice_method)};
                interp.ClearPendingException();
                const DexValue h_virtual = interp.Execute(unreflect, ua, 2);
                Check(!interp.HasPendingException(),
                      std::string("unreflect threw nothing: ") +
                          interp.DescribePendingException());
                interp.ClearPendingException();
                Check(h_virtual.l != nullptr, "unreflect returned a MethodHandle");

                DexClass* handle_class = linker.ClassOfObject(h_virtual.l);
                bind_to = handle_class != nullptr
                              ? handle_class->FindVirtualMethod(
                                    "bindTo",
                                    "(Ljava/lang/Object;)Ljava/lang/invoke/MethodHandle;")
                              : nullptr;
                invoke_with = handle_class != nullptr
                                  ? handle_class->FindVirtualMethod(
                                        "invokeWithArguments",
                                        "([Ljava/lang/Object;)Ljava/lang/Object;")
                                  : nullptr;
                Check(bind_to != nullptr && invoke_with != nullptr,
                      "bindTo and invokeWithArguments are reachable on the handle");

                // unreflectSpecial -> non-virtual
                DexValue sa[3] = {DexValue::Ref(lookup), DexValue::Ref(twice_method),
                                  DexValue::Ref(reinterpret_cast<DexObject*>(
                                      linker.GetClassObject(greeter)))};
                interp.ClearPendingException();
                const DexValue h_special = interp.Execute(unreflect_special, sa, 3);
                Check(!interp.HasPendingException(),
                      std::string("unreflectSpecial threw nothing: ") +
                          interp.DescribePendingException());
                interp.ClearPendingException();
                Check(h_special.l != nullptr, "unreflectSpecial returned a MethodHandle");

                if (bind_to != nullptr && invoke_with != nullptr) {
                    const int got_virtual =
                        CallHandle(&interp, &linker, bind_to, invoke_with, h_virtual.l,
                                   receiver, 21);
                    Check(got_virtual == 2100,
                          std::string("unreflect dispatched virtually to Override.twice "
                                      "-> 2100, got ") + std::to_string(got_virtual));
                    const int got_special =
                        CallHandle(&interp, &linker, bind_to, invoke_with, h_special.l,
                                   receiver, 21);
                    Check(got_special == 42,
                          std::string("unreflectSpecial ran Greeter.twice itself -> 42, got ") +
                              std::to_string(got_special));
                    // Stated separately: the two must DIFFER, which is the property the
                    // flag exists for. Equal values would mean the flag did nothing even if
                    // one of the numbers happened to look right.
                    Check(got_virtual != got_special,
                          "the two dispatch modes reached different bodies");
                }
            }
        }
    }

    // ── the case that hangs a real app: a default method on a PROXY ──
    //
    // A proxy class declares no bodies, so anything invoked on it is forwarded to the
    // InvocationHandler. For an ABSTRACT interface method that is correct and the handler's
    // answer (7) is the result. For a DEFAULT method it is a trap: the handler is being
    // asked to run the default, and forwarding puts it straight back into itself. That is
    // the unbounded recursion Unity's JNIBridge guards against by reaching for
    // unreflectSpecial, and it is what must reach Greeter.twice's own body instead.
    {
        std::printf("[unreflectSpecial] a default method runs on a proxy without recursing\n");
        DexClass* greeter = linker.FindClass("Lcom/foo/Greeter;");
        DexClass* handler_class = linker.FindClass("Lcom/foo/H;");
        DexClass* proxy_base = linker.FindClass("Ljava/lang/reflect/Proxy;");
        DexMethod* factory =
            proxy_base != nullptr
                ? proxy_base->FindDirectMethod(
                      "newProxyInstance",
                      "(Ljava/lang/ClassLoader;[Ljava/lang/Class;"
                      "Ljava/lang/reflect/InvocationHandler;)Ljava/lang/Object;")
                : nullptr;
        Check(greeter != nullptr && handler_class != nullptr && factory != nullptr,
              "the test classes and Proxy.newProxyInstance are available");

        if (greeter != nullptr && handler_class != nullptr && factory != nullptr) {
            DexObject* handler = linker.AllocObject(handler_class);
            DexValue pargs[3];
            pargs[0] = DexValue::Ref(nullptr);
            pargs[1] = DexValue::Ref(NewClassArray({greeter}));
            pargs[2] = DexValue::Ref(handler);
            interp.ClearPendingException();
            const DexValue made = interp.Execute(factory, pargs, 3);
            interp.ClearPendingException();
            DexObject* proxy = made.l;
            Check(proxy != nullptr, "created a Greeter proxy");

            // The baseline: an ABSTRACT interface method on the proxy does reach the
            // handler. Pinned so the 42 below is known to be a different route, not the
            // handler happening to return it.
            DexMethod* describe = greeter->FindVirtualMethod("describe", "()I");
            if (proxy != nullptr && describe != nullptr) {
                DexValue da = DexValue::Ref(proxy);
                interp.ClearPendingException();
                const DexValue got = interp.Execute(describe, &da, 1);
                const bool threw = interp.HasPendingException();
                interp.ClearPendingException();
                Check(!threw && got.i == 7,
                      std::string("an abstract method on the proxy reaches the handler -> 7,"
                                  " got ") + std::to_string(got.i));
            }

            // And the default, through a special handle bound to that same proxy.
            if (proxy != nullptr && lookup != nullptr && twice_method != nullptr &&
                bind_to != nullptr && invoke_with != nullptr) {
                DexClass* lookup_class = linker.ClassOfObject(lookup);
                DexMethod* unreflect_special =
                    lookup_class != nullptr
                        ? lookup_class->FindVirtualMethod(
                              "unreflectSpecial",
                              "(Ljava/lang/reflect/Method;Ljava/lang/Class;)"
                              "Ljava/lang/invoke/MethodHandle;")
                        : nullptr;
                if (unreflect_special != nullptr) {
                    DexValue sa[3] = {DexValue::Ref(lookup), DexValue::Ref(twice_method),
                                      DexValue::Ref(reinterpret_cast<DexObject*>(
                                          linker.GetClassObject(greeter)))};
                    interp.ClearPendingException();
                    const DexValue h = interp.Execute(unreflect_special, sa, 3);
                    interp.ClearPendingException();
                    const int got = CallHandle(&interp, &linker, bind_to, invoke_with, h.l,
                                               proxy, 21);
                    Check(got == 42,
                          std::string("the default's own body ran on the proxy -> 42, got ") +
                              std::to_string(got) +
                              " (7 would mean it recursed into the handler)");
                }
            }
        }
    }

    std::printf("=== %s (%d checks, %d error) ===\n",
                g_failures == 0 ? "PASSED" : "FAILED", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
