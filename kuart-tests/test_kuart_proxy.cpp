// Host test for dynamic proxies: Proxy.newProxyInstance + InvocationHandler dispatch.
//
// A proxy class is synthesised by the linker (DexClassLinker::GetOrCreateProxyClass)
// with no method bodies at all, and the interpreter is what makes it work: a call
// landing on a bodyless method whose receiver is a proxy is forwarded to the
// instance's InvocationHandler instead of raising AbstractMethodError.
//
// That distinction is the whole feature, and it is easy to get wrong in a way that
// compiles: the previous implementation returned a bare `new Proxy(h)`, which does
// not implement the requested interface, so a cast to it failed and — where the DEX
// had no cast — the first interface call died with "method without body: run". Unity
// hit exactly that through Activity.runOnUiThread.
//
// framework.dex is loaded because Proxy, InvocationHandler and Integer must be the
// real classes; the synthetic DEX adds the interface being proxied and a handler
// with a genuine bytecode body.
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

void Check(bool ok, const std::string& what) {
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
void Op21c(std::vector<uint16_t>* code, uint8_t op, uint8_t a, uint16_t idx) {
    code->push_back(static_cast<uint16_t>(op | (a << 8)));
    code->push_back(idx);
}
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
constexpr uint8_t kOpReturnVoid = 0x0e;
constexpr uint8_t kOpReturn = 0x0f;
constexpr uint8_t kOpReturnObject = 0x11;
constexpr uint8_t kOpMoveResult = 0x0a;
constexpr uint8_t kOpMoveResultObject = 0x0c;
constexpr uint8_t kOpSputObject = 0x69;
constexpr uint8_t kOpInvokeStatic = 0x71;
constexpr uint8_t kOpInvokeInterface = 0x72;

constexpr uint32_t kAccPublic = 0x1;
constexpr uint32_t kAccStatic = 0x8;
constexpr uint32_t kAccPublicStatic = kAccPublic | kAccStatic;
constexpr uint32_t kAccInterface = 0x200;
constexpr uint32_t kAccAbstract = 0x400;

struct Specs {
    // Static slots the handler writes so the test can inspect what it received.
    FieldSpec last_method{"lastMethod", "Ljava/lang/reflect/Method;", kAccPublicStatic};
    FieldSpec last_args{"lastArgs", "[Ljava/lang/Object;", kAccPublicStatic};
    FieldSpec last_proxy{"lastProxy", "Ljava/lang/Object;", kAccPublicStatic};

    MethodSpec object_ctor;
    MethodSpec handler_ctor;
    MethodSpec handler_invoke;   // the real InvocationHandler.invoke body
    MethodSpec calc_add;         // interface method, abstract
    MethodSpec calc_run;         // interface method returning void
    MethodSpec caller_add;       // bytecode that does invoke-interface on the proxy

    // Integer.valueOf(int), so the handler can return a boxed int.
    MethodRefSpec integer_value_of{"Ljava/lang/Integer;", "valueOf", "Ljava/lang/Integer;", {"I"}};

    Specs() {
        object_ctor.name = "<init>";
        object_ctor.access_flags = kAccPublic | 0x10000;  // ACC_CONSTRUCTOR

        handler_ctor.name = "<init>";
        handler_ctor.access_flags = kAccPublic | 0x10000;

        handler_invoke.name = "invoke";
        handler_invoke.return_type = "Ljava/lang/Object;";
        handler_invoke.params = {"Ljava/lang/Object;", "Ljava/lang/reflect/Method;",
                                "[Ljava/lang/Object;"};
        handler_invoke.access_flags = kAccPublic;

        calc_add.name = "add";
        calc_add.return_type = "I";
        calc_add.params = {"I", "I"};
        calc_add.access_flags = kAccPublic | kAccAbstract;

        calc_run.name = "run";
        calc_run.return_type = "V";
        calc_run.access_flags = kAccPublic | kAccAbstract;

        caller_add.name = "callAdd";
        caller_add.return_type = "I";
        caller_add.params = {"Lcom/foo/Calc;"};
        caller_add.access_flags = kAccPublicStatic;
    }
};

std::vector<ClassSpec> BuildClasses(const Specs& s) {
    ClassSpec calc;
    calc.descriptor = "Lcom/foo/Calc;";
    calc.access_flags = kAccPublic | kAccInterface | kAccAbstract;
    calc.virtual_methods = {s.calc_add, s.calc_run};

    ClassSpec handler;
    handler.descriptor = "Lcom/foo/H;";
    handler.interfaces = {"Ljava/lang/reflect/InvocationHandler;"};
    handler.static_fields = {s.last_method, s.last_args, s.last_proxy};
    handler.direct_methods = {s.handler_ctor};
    handler.virtual_methods = {s.handler_invoke};
    handler.extra_method_refs = {s.integer_value_of};

    ClassSpec caller;
    caller.descriptor = "Lcom/foo/Caller;";
    caller.direct_methods = {s.caller_add};

    return {calc, handler, caller};
}

DexClassLinker* g_linker = nullptr;

DexObject* NewClassArray(const std::vector<DexClass*>& classes) {
    DexClass* array_class = g_linker->FindClass("[Ljava/lang/Class;");
    if (array_class == nullptr) return nullptr;
    DexArray* arr = g_linker->AllocArray(array_class, static_cast<int32_t>(classes.size()));
    if (arr == nullptr) return nullptr;
    auto** data = reinterpret_cast<DexObject**>(arr + 1);
    for (size_t i = 0; i < classes.size(); ++i) {
        data[i] = g_linker->GetClassObject(classes[i]);
    }
    return reinterpret_cast<DexObject*>(arr);
}

}  // namespace

int main() {
    std::printf("=== KuART dynamic proxy: newProxyInstance + handler dispatch ===\n");

    Specs probe;
    DexBuilder index_builder;
    index_builder.Build(BuildClasses(probe));

    const uint16_t kFieldLastMethod =
        static_cast<uint16_t>(index_builder.FieldIndexOf("Lcom/foo/H;", probe.last_method));
    const uint16_t kFieldLastArgs =
        static_cast<uint16_t>(index_builder.FieldIndexOf("Lcom/foo/H;", probe.last_args));
    const uint16_t kFieldLastProxy =
        static_cast<uint16_t>(index_builder.FieldIndexOf("Lcom/foo/H;", probe.last_proxy));
    const uint16_t kIntegerValueOf =
        static_cast<uint16_t>(index_builder.MethodRefIndexOf(probe.integer_value_of));
    const uint16_t kCalcAdd =
        static_cast<uint16_t>(index_builder.MethodIndexOf("Lcom/foo/Calc;", probe.calc_add));

    Specs s;
    s.handler_ctor.code = {Op11x(kOpReturnVoid, 0)};
    s.handler_ctor.registers_size = 1;
    s.handler_ctor.ins_size = 1;

    // H.invoke(proxy, method, args):
    //   H.lastProxy = proxy; H.lastMethod = method; H.lastArgs = args;
    //   return Integer.valueOf(42);
    //
    // Registers: v0 scratch, p0=this(v1), p1=proxy(v2), p2=method(v3), p3=args(v4).
    {
        std::vector<uint16_t> c;
        Op21c(&c, kOpSputObject, 2, kFieldLastProxy);
        Op21c(&c, kOpSputObject, 3, kFieldLastMethod);
        Op21c(&c, kOpSputObject, 4, kFieldLastArgs);
        Op21s(&c, kOpConst16, 0, 42);
        Op35c(&c, kOpInvokeStatic, kIntegerValueOf, {0});
        c.push_back(Op11x(kOpMoveResultObject, 0));
        c.push_back(Op11x(kOpReturnObject, 0));
        s.handler_invoke.code = c;
        s.handler_invoke.registers_size = 5;
        s.handler_invoke.ins_size = 4;
        s.handler_invoke.outs_size = 1;
    }

    // Caller.callAdd(Calc c): return c.add(1, 2);
    // Registers: v0 scratch/result, v1=const 1, v2=const 2, p0=c(v3).
    {
        std::vector<uint16_t> c;
        Op21s(&c, kOpConst16, 1, 1);
        Op21s(&c, kOpConst16, 2, 2);
        Op35c(&c, kOpInvokeInterface, kCalcAdd, {3, 1, 2});
        c.push_back(Op11x(kOpMoveResult, 0));
        c.push_back(Op11x(kOpReturn, 0));
        s.caller_add.code = c;
        s.caller_add.registers_size = 4;
        s.caller_add.ins_size = 1;
        s.caller_add.outs_size = 3;
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

    DexClass* proxy_base = linker.FindClass("Ljava/lang/reflect/Proxy;");
    Check(proxy_base != nullptr && !proxy_base->is_stub,
          "java.lang.reflect.Proxy is real, not an auto-stub");
    DexClass* calc = linker.FindClass("Lcom/foo/Calc;");
    DexClass* handler_class = linker.FindClass("Lcom/foo/H;");
    if (calc == nullptr || handler_class == nullptr || proxy_base == nullptr) {
        std::printf("  FAIL missing test classes\n=== FAILED ===\n");
        return 1;
    }

    // ── newProxyInstance returns something that IS the interface ──
    DexObject* proxy = nullptr;
    {
        DexMethod* factory = proxy_base->FindDirectMethod(
            "newProxyInstance",
            "(Ljava/lang/ClassLoader;[Ljava/lang/Class;Ljava/lang/reflect/InvocationHandler;)"
            "Ljava/lang/Object;");
        Check(factory != nullptr, "found Proxy.newProxyInstance");
        if (factory == nullptr) {
            std::printf("=== FAILED ===\n");
            return 1;
        }

        DexObject* handler = linker.AllocObject(handler_class);
        Check(handler != nullptr, "allocated the InvocationHandler");

        DexValue args[3];
        args[0] = DexValue::Ref(nullptr);                       // ClassLoader
        args[1] = DexValue::Ref(NewClassArray({calc}));         // Class[]{Calc.class}
        args[2] = DexValue::Ref(handler);
        interp.ClearPendingException();
        const DexValue made = interp.Execute(factory, args, 3);
        Check(!interp.HasPendingException(), "newProxyInstance threw nothing");
        interp.ClearPendingException();
        proxy = made.l;
        Check(proxy != nullptr, "newProxyInstance returned an object");
        if (proxy == nullptr) {
            std::printf("=== FAILED ===\n");
            return 1;
        }

        DexClass* proxy_class = linker.ClassOfObject(proxy);
        Check(proxy_class != nullptr, "the proxy has a usable class");
        Check(proxy_class != nullptr && proxy_class->is_proxy,
              "the class is flagged as a proxy");
        // This is the check the old Java implementation failed: the returned object
        // must satisfy a cast to the proxied interface, or every call site breaks.
        Check(proxy_class != nullptr && proxy_class->IsSubClassOf(calc),
              "the proxy IS-A com.foo.Calc — a checked cast would succeed");
        Check(proxy_class != nullptr && proxy_class->IsSubClassOf(proxy_base),
              "the proxy IS-A java.lang.reflect.Proxy");
    }

    // ── calling an interface method reaches the handler and unboxes the result ──
    {
        DexMethod* add = calc->FindVirtualMethod("add", "(II)I");
        Check(add != nullptr, "found Calc.add");
        Check(add != nullptr && add->code_item == nullptr,
              "Calc.add is abstract — it has no body to run");

        DexValue args[3] = {DexValue::Ref(proxy), DexValue::Int(1), DexValue::Int(2)};
        interp.ClearPendingException();
        const DexValue result = interp.Execute(add, args, 3);
        Check(!interp.HasPendingException(),
              std::string("calling a proxied method threw nothing: ") +
                  interp.DescribePendingException());
        interp.ClearPendingException();
        // 42 is what the handler returns, boxed as Integer; reaching it proves both
        // that dispatch happened and that the boxed result was unboxed to int.
        Check(result.i == 42, "the handler's boxed Integer(42) came back as int 42");
    }

    // ── the handler received a real Method and a correctly boxed Object[] ──
    {
        DexObject* seen_proxy = handler_class->static_values.empty()
                                    ? nullptr
                                    : nullptr;
        (void)seen_proxy;
        kudroid::kuart::DexField* proxy_field =
            handler_class->FindStaticField("lastProxy", "Ljava/lang/Object;");
        kudroid::kuart::DexField* method_field =
            handler_class->FindStaticField("lastMethod", "Ljava/lang/reflect/Method;");
        kudroid::kuart::DexField* args_field =
            handler_class->FindStaticField("lastArgs", "[Ljava/lang/Object;");
        Check(proxy_field != nullptr && method_field != nullptr && args_field != nullptr,
              "the handler's static slots exist");
        if (proxy_field == nullptr || method_field == nullptr || args_field == nullptr) {
            std::printf("=== FAILED ===\n");
            return 1;
        }

        DexObject* recorded_proxy =
            handler_class->static_values[proxy_field->offset_or_slot].l;
        Check(recorded_proxy == proxy,
              "the handler was passed the proxy itself as the first argument");

        DexObject* recorded_method =
            handler_class->static_values[method_field->offset_or_slot].l;
        Check(recorded_method != nullptr,
              "the handler was passed a non-null java.lang.reflect.Method");
        if (recorded_method != nullptr) {
            // A handler that switches on method.getName() is the common shape, so the
            // name has to be right — passing null or a nameless Method would break it.
            DexClass* method_class = linker.ClassOfObject(recorded_method);
            kudroid::kuart::DexField* name_field =
                method_class != nullptr
                    ? method_class->FindInstanceField("name", "Ljava/lang/String;")
                    : nullptr;
            auto* name = name_field != nullptr
                             ? reinterpret_cast<DexString*>(
                                   recorded_method->GetField<DexObject*>(
                                       name_field->offset_or_slot))
                             : nullptr;
            Check(name != nullptr && name->utf8 != nullptr &&
                      std::strcmp(name->utf8, "add") == 0,
                  std::string("Method.getName() is \"add\", got \"") +
                      (name != nullptr && name->utf8 != nullptr ? name->utf8 : "(null)") + "\"");
        }

        auto* recorded_args = reinterpret_cast<DexArray*>(
            handler_class->static_values[args_field->offset_or_slot].l);
        Check(recorded_args != nullptr, "the handler was passed an Object[]");
        Check(recorded_args != nullptr && recorded_args->length == 2,
              "the Object[] holds exactly the two declared parameters, not the receiver");
        if (recorded_args != nullptr && recorded_args->length == 2) {
            auto** items = reinterpret_cast<DexObject**>(recorded_args + 1);
            // Both ints must arrive boxed: a raw int in a reference slot would be
            // read as a pointer by any handler that touches it.
            DexClass* integer_class = linker.FindClass("Ljava/lang/Integer;");
            Check(items[0] != nullptr && linker.ClassOfObject(items[0]) == integer_class,
                  "argument 0 was boxed into an Integer");
            Check(items[1] != nullptr && linker.ClassOfObject(items[1]) == integer_class,
                  "argument 1 was boxed into an Integer");
        }
    }

    // ── the bytecode invoke-interface path dispatches too ──
    // Execute() is not the only entry: real code reaches a proxy through
    // invoke-interface, which resolves the target through the receiver's class.
    {
        DexClass* caller = linker.FindClass("Lcom/foo/Caller;");
        DexMethod* call_add =
            caller != nullptr ? caller->FindDirectMethod("callAdd", "(Lcom/foo/Calc;)I") : nullptr;
        Check(call_add != nullptr, "found Caller.callAdd");
        if (call_add != nullptr) {
            DexValue arg = DexValue::Ref(proxy);
            interp.ClearPendingException();
            const DexValue result = interp.Execute(call_add, &arg, 1);
            Check(!interp.HasPendingException(),
                  std::string("invoke-interface on a proxy threw nothing: ") +
                      interp.DescribePendingException());
            interp.ClearPendingException();
            Check(result.i == 42, "invoke-interface on a proxy returned 42");
        }
    }

    // ── a void method must not fail on the handler's non-null return ──
    {
        DexMethod* run = calc->FindVirtualMethod("run", "()V");
        Check(run != nullptr, "found Calc.run");
        if (run != nullptr) {
            DexValue arg = DexValue::Ref(proxy);
            interp.ClearPendingException();
            interp.Execute(run, &arg, 1);
            Check(!interp.HasPendingException(),
                  "a void proxied method ignores the handler's return value");
            interp.ClearPendingException();
        }
    }

    // ── the static helpers agree with the object that was made ──
    {
        DexMethod* is_proxy = proxy_base->FindDirectMethod("isProxyClass",
                                                           "(Ljava/lang/Class;)Z");
        Check(is_proxy != nullptr, "found Proxy.isProxyClass");
        if (is_proxy != nullptr) {
            DexClass* proxy_class = linker.ClassOfObject(proxy);
            DexValue arg = DexValue::Ref(linker.GetClassObject(proxy_class));
            interp.ClearPendingException();
            Check(interp.Execute(is_proxy, &arg, 1).i == 1,
                  "isProxyClass(proxy class) is true");
            DexValue plain = DexValue::Ref(linker.GetClassObject(calc));
            Check(interp.Execute(is_proxy, &plain, 1).i == 0,
                  "isProxyClass(ordinary interface) is false");
            interp.ClearPendingException();
        }

        DexMethod* get_handler = proxy_base->FindDirectMethod(
            "getInvocationHandler", "(Ljava/lang/Object;)Ljava/lang/reflect/InvocationHandler;");
        Check(get_handler != nullptr, "found Proxy.getInvocationHandler");
        if (get_handler != nullptr) {
            DexValue arg = DexValue::Ref(proxy);
            interp.ClearPendingException();
            const DexValue h = interp.Execute(get_handler, &arg, 1);
            Check(!interp.HasPendingException() && h.l != nullptr,
                  "getInvocationHandler returns the handler that was installed");
            Check(h.l != nullptr && linker.ClassOfObject(h.l) == handler_class,
                  "and it is the com.foo.H instance");
            interp.ClearPendingException();
        }
    }

    // ── two proxies for the same interfaces share one class ──
    // getProxyClass is specified to be stable for a given interface list, and code
    // caches on it; a fresh class per call would also leak one class per proxy.
    {
        DexMethod* get_class = proxy_base->FindDirectMethod(
            "getProxyClass", "(Ljava/lang/ClassLoader;[Ljava/lang/Class;)Ljava/lang/Class;");
        Check(get_class != nullptr, "found Proxy.getProxyClass");
        if (get_class != nullptr) {
            DexValue a[2] = {DexValue::Ref(nullptr), DexValue::Ref(NewClassArray({calc}))};
            DexValue b[2] = {DexValue::Ref(nullptr), DexValue::Ref(NewClassArray({calc}))};
            interp.ClearPendingException();
            const DexValue first = interp.Execute(get_class, a, 2);
            const DexValue second = interp.Execute(get_class, b, 2);
            Check(!interp.HasPendingException(), "getProxyClass threw nothing");
            Check(first.l != nullptr && first.l == second.l,
                  "the same interface list yields the same proxy class");
            interp.ClearPendingException();
        }
    }

    // ── a bodyless method on a NON-proxy still reports the defect ──
    // The proxy path must not swallow the AbstractMethodError that a genuinely
    // abstract call deserves, or a real bug becomes a silent zero.
    {
        DexObject* plain = linker.AllocObject(handler_class);
        DexMethod* add = calc->FindVirtualMethod("add", "(II)I");
        DexValue args[3] = {DexValue::Ref(plain), DexValue::Int(1), DexValue::Int(2)};
        interp.ClearPendingException();
        interp.Execute(add, args, 3);
        Check(interp.HasPendingException(),
              "an abstract method on a non-proxy receiver still throws");
        interp.ClearPendingException();
    }

    // ── a Class[] holding RAW jclass handles, the way native code builds one ──
    //
    // This is the crash. Unity reaches Proxy.newProxyInstance through JNI, and native
    // code holds a class as a raw DexClass* — that is what a jclass IS (see
    // DexJniEnv.h). So the Class[] it fills contains DexClass pointers, not the heap
    // java.lang.Class objects that bytecode would put there. Both denote the same class
    // and both are legal to hold; the array is not malformed.
    //
    // ClassOf's fallback was `class_object->clazz`, and DexClass::descriptor sits at the
    // same offset 0 as DexObject::clazz — so each element resolved to its own descriptor
    // STRING pointer, which was then used as a DexClass*. GetOrCreateProxyClass appended
    // iface->descriptor (offset 0 of the string) to its cache key, and strlen faulted at
    // 0x64696f72646e6140: the 16-byte-aligned form of 0x64696f72646e614c, the bytes
    // `Landroid`. pc was _platform_strlen+0x4 with lr in basic_string::append.
    //
    // Every element here is a raw jclass, so nothing about this test passes by accident:
    // the old code could not get through it without dereferencing a string.
    {
        DexClass* array_class = linker.FindClass("[Ljava/lang/Class;");
        Check(array_class != nullptr, "the Class[] array class exists");
        DexMethod* factory = proxy_base->FindDirectMethod(
            "newProxyInstance",
            "(Ljava/lang/ClassLoader;[Ljava/lang/Class;Ljava/lang/reflect/InvocationHandler;)"
            "Ljava/lang/Object;");
        if (array_class != nullptr && factory != nullptr) {
            DexArray* raw = linker.AllocArray(array_class, 1);
            Check(raw != nullptr, "allocated a Class[1]");
            if (raw != nullptr) {
                // A jclass, exactly as GetObjectArrayElement or a returned Class<?>
                // would hand it to a guest library.
                reinterpret_cast<DexObject**>(raw + 1)[0] =
                    reinterpret_cast<DexObject*>(calc);

                DexObject* handler = linker.AllocObject(handler_class);
                DexValue args[3] = {DexValue::Ref(nullptr), DexValue::Ref(raw),
                                    DexValue::Ref(handler)};
                interp.ClearPendingException();
                const DexValue made = interp.Execute(factory, args, 3);
                Check(!interp.HasPendingException(),
                      "newProxyInstance accepts a Class[] of raw jclass handles");
                Check(made.l != nullptr, "and returns a proxy");

                DexClass* made_class = linker.ClassOfObject(made.l);
                Check(made_class != nullptr && made_class->is_proxy,
                      "the result is a proxy class");
                // The interface was resolved to the REAL class, not to something derived
                // from its descriptor bytes. Without this the proxy would not implement
                // Calc and every call site through it would break.
                Check(made_class != nullptr && made_class->IsSubClassOf(calc),
                      "the proxy IS-A com.foo.Calc, so the jclass resolved correctly");
                interp.ClearPendingException();
            }
        }
    }

    // ── an object that is not a class at all is refused, not guessed at ──
    //
    // The old fallback answered "the class OF this object", so a String element reported
    // java.lang.String as though the caller had passed String.class. A wrong answer
    // dressed as a right one: the proxy would be built for the wrong interface and the
    // failure would surface somewhere else entirely.
    {
        DexClass* array_class = linker.FindClass("[Ljava/lang/Class;");
        DexMethod* factory = proxy_base->FindDirectMethod(
            "newProxyInstance",
            "(Ljava/lang/ClassLoader;[Ljava/lang/Class;Ljava/lang/reflect/InvocationHandler;)"
            "Ljava/lang/Object;");
        if (array_class != nullptr && factory != nullptr) {
            DexArray* bad = linker.AllocArray(array_class, 1);
            if (bad != nullptr) {
                // A plain instance where a Class was expected.
                reinterpret_cast<DexObject**>(bad + 1)[0] =
                    linker.AllocObject(handler_class);

                DexObject* handler = linker.AllocObject(handler_class);
                DexValue args[3] = {DexValue::Ref(nullptr), DexValue::Ref(bad),
                                    DexValue::Ref(handler)};
                interp.ClearPendingException();
                interp.Execute(factory, args, 3);
                Check(interp.HasPendingException(),
                      "a non-class element throws instead of being read as its own class");
                interp.ClearPendingException();
            }
        }
    }

    std::printf("=== %s (%d error) ===\n", g_failures == 0 ? "PASSED" : "FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}
