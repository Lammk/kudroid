// Host test for the libcore natives, driven the way real Java calls them.
// The DEX declares only the classes/signatures LibCore.cpp dispatches on (no d8 here).
#include "kudroid/kuart/DexClassLinker.h"
#include "kudroid/kuart/DexObject.h"
#include "kudroid/kuart/DexString.h"
#include "kudroid/kuart/Interpreter.h"
#include "kudroid/kuart/VmLock.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
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

// ACC_PUBLIC | ACC_NATIVE, and the static variant.
constexpr uint32_t kAccNative = 0x1 | 0x100;
constexpr uint32_t kAccStaticNative = 0x1 | 0x8 | 0x100;

MethodSpec Native(const char* name, const char* ret,
                  const std::vector<std::string>& params, bool is_static = false) {
    MethodSpec m;
    m.name = name;
    m.return_type = ret;
    m.params = params;
    m.access_flags = is_static ? kAccStaticNative : kAccNative;
    return m;
}

MethodSpec Ctor() {
    MethodSpec m;
    m.name = "<init>";
    m.access_flags = 0x10001;  // ACC_PUBLIC | ACC_CONSTRUCTOR
    m.code = {0x000e};         // return-void
    m.registers_size = 1;
    m.ins_size = 1;
    return m;
}

std::vector<ClassSpec> BuildClasses() {
    ClassSpec object;
    object.descriptor = "Ljava/lang/Object;";
    object.superclass = "";
    object.direct_methods = {Ctor()};
    object.virtual_methods = {
        Native("getClass", "Ljava/lang/Class;", {}),
        Native("hashCode", "I", {}),
        Native("clone", "Ljava/lang/Object;", {}),
        Native("notify", "V", {}),
        Native("notifyAll", "V", {}),
        Native("wait", "V", {}),
    };

    ClassSpec string;
    string.descriptor = "Ljava/lang/String;";
    string.direct_methods = {
        Ctor(),
        Native("initCopy", "V", {"Ljava/lang/String;"}),
        Native("initChars", "V", {"[C", "I", "I"}),
        Native("initBytes", "V", {"[B", "I", "I"}),
    };
    string.virtual_methods = {
        Native("length", "I", {}),
        Native("charAt", "C", {"I"}),
        Native("substring", "Ljava/lang/String;", {"I", "I"}),
        Native("concat", "Ljava/lang/String;", {"Ljava/lang/String;"}),
        Native("trim", "Ljava/lang/String;", {}),
        Native("toUpperCase", "Ljava/lang/String;", {}),
        Native("toLowerCase", "Ljava/lang/String;", {}),
        Native("compareTo", "I", {"Ljava/lang/String;"}),
        Native("equalsIgnoreCase", "Z", {"Ljava/lang/String;"}),
        Native("indexOf", "I", {"I", "I"}),
        Native("lastIndexOf", "I", {"I", "I"}),
        Native("intern", "Ljava/lang/String;", {}),
        Native("getBytes", "[B", {}),
        Native("toCharArray", "[C", {}),
    };

    ClassSpec class_class;
    class_class.descriptor = "Ljava/lang/Class;";
    class_class.virtual_methods = {
        Native("getName", "Ljava/lang/String;", {}),
        Native("getModifiers", "I", {}),
        Native("isArray", "Z", {}),
    };

    ClassSpec system;
    system.descriptor = "Ljava/lang/System;";
    system.direct_methods = {
        Native("currentTimeMillis", "J", {}, /*is_static=*/true),
        Native("nanoTime", "J", {}, true),
        Native("identityHashCode", "I", {"Ljava/lang/Object;"}, true),
        Native("arraycopy", "V",
               {"Ljava/lang/Object;", "I", "Ljava/lang/Object;", "I", "I"}, true),
        Native("getProperty", "Ljava/lang/String;", {"Ljava/lang/String;"}, true),
    };

    ClassSpec math;
    math.descriptor = "Ljava/lang/Math;";
    math.direct_methods = {
        Native("sqrt", "D", {"D"}, true),
        Native("pow", "D", {"D", "D"}, true),
        Native("floor", "D", {"D"}, true),
    };

    ClassSpec dbl;
    dbl.descriptor = "Ljava/lang/Double;";
    dbl.direct_methods = {
        Native("doubleToLongBits", "J", {"D"}, true),
        Native("longBitsToDouble", "D", {"J"}, true),
        Native("toString", "Ljava/lang/String;", {"D"}, true),
        Native("parseDouble", "D", {"Ljava/lang/String;"}, true),
    };

    ClassSpec flt;
    flt.descriptor = "Ljava/lang/Float;";
    flt.direct_methods = {
        Native("floatToIntBits", "I", {"F"}, true),
        Native("intBitsToFloat", "F", {"I"}, true),
    };

    // Boxing goes through the real valueOf/xxxValue pair, so reflection tests
    // need Integer to be a working class rather than a stub.
    ClassSpec integer;
    integer.descriptor = "Ljava/lang/Integer;";
    integer.instance_fields = {FieldSpec{"value", "I", 0x12}};

    ClassSpec point;
    point.descriptor = "Lcom/foo/Point;";
    point.instance_fields = {FieldSpec{"x", "I", 0x1}, FieldSpec{"y", "I", 0x1}};
    point.direct_methods = {Ctor()};

    ClassSpec throwable;
    throwable.descriptor = "Ljava/lang/Throwable;";
    throwable.direct_methods = {Ctor()};

    ClassSpec monitor_state;
    monitor_state.descriptor = "Ljava/lang/IllegalMonitorStateException;";
    monitor_state.superclass = "Ljava/lang/Throwable;";
    monitor_state.direct_methods = {Ctor()};

    // DirectByteBuffer natives must back the buffer with a real host malloc:
    // FMOD's fmodProcess mixes PCM through GetDirectBufferAddress().
    ClassSpec dbb;
    dbb.descriptor = "Ljava/nio/DirectByteBuffer;";
    dbb.direct_methods = {
        Native("nAllocate", "J", {"I"}, true),
        Native("nGet", "B", {"J", "I"}, true),
        Native("nPut", "V", {"J", "I", "B"}, true),
        Native("nGetArray", "V", {"J", "I", "[B", "I", "I"}, true),
        Native("nPutArray", "V", {"J", "I", "[B", "I", "I"}, true),
    };

    // AssetManager root backs jar:file:// reads of extracted assets.
    ClassSpec assets;
    assets.descriptor = "Landroid/content/res/AssetManager;";
    assets.direct_methods = {
        Native("nativeGetAssetsDir", "Ljava/lang/String;", {}, true),
    };

    return {object, string,  class_class, system,    math, dbl,
            flt,    integer, point,       throwable, monitor_state, dbb,
            assets};
}

// ── helpers ──────────────────────────────────────────────────────────────────

DexMethod* FindNative(DexClass* klass, const char* name) {
    if (klass == nullptr) return nullptr;
    for (DexMethod& m : klass->virtual_methods) {
        if (std::strcmp(m.name, name) == 0) return &m;
    }
    for (DexMethod& m : klass->direct_methods) {
        if (std::strcmp(m.name, name) == 0) return &m;
    }
    return nullptr;
}

struct Vm {
    DexClassLinker linker;
    Interpreter interp{&linker};

    DexValue Call(const char* class_desc, const char* method,
                  const std::vector<DexValue>& args) {
        DexClass* klass = linker.FindClass(class_desc);
        DexMethod* m = FindNative(klass, method);
        if (m == nullptr) {
            Check(false, std::string("method not found: ") + class_desc + "." + method);
            return DexValue();
        }
        interp.ClearPendingException();
        return interp.Execute(m, args.data(), args.size());
    }

    DexString* Str(const char* utf8) { return linker.NewString(utf8); }

    std::string Utf8(const DexValue& v) {
        auto* s = reinterpret_cast<DexString*>(v.l);
        return (s != nullptr && s->utf8 != nullptr) ? std::string(s->utf8, s->length)
                                                    : std::string("<null>");
    }
};

void TestString(Vm& vm) {
    std::printf("-- java.lang.String --\n");

    DexString* hello = vm.Str("Hello");
    Check(vm.Call("Ljava/lang/String;", "length", {DexValue::Ref(hello)}).i == 5,
          "length(\"Hello\") == 5");
    Check(vm.Call("Ljava/lang/String;", "charAt",
                  {DexValue::Ref(hello), DexValue::Int(1)}).i == 'e',
          "charAt(1) == 'e'");

    // Out-of-range access must throw, not read past the buffer.
    vm.Call("Ljava/lang/String;", "charAt", {DexValue::Ref(hello), DexValue::Int(9)});
    Check(vm.interp.HasPendingException(), "charAt(9) throws");
    vm.interp.ClearPendingException();

    Check(vm.Utf8(vm.Call("Ljava/lang/String;", "substring",
                          {DexValue::Ref(hello), DexValue::Int(1), DexValue::Int(4)})) ==
              "ell",
          "substring(1, 4) == \"ell\"");
    Check(vm.Utf8(vm.Call("Ljava/lang/String;", "concat",
                          {DexValue::Ref(hello), DexValue::Ref(vm.Str("!"))})) == "Hello!",
          "concat(\"!\") == \"Hello!\"");
    Check(vm.Utf8(vm.Call("Ljava/lang/String;", "trim",
                          {DexValue::Ref(vm.Str("  pad \t"))})) == "pad",
          "trim() strips both ends");
    Check(vm.Utf8(vm.Call("Ljava/lang/String;", "toUpperCase",
                          {DexValue::Ref(hello)})) == "HELLO",
          "toUpperCase() == \"HELLO\"");
    Check(vm.Utf8(vm.Call("Ljava/lang/String;", "toLowerCase",
                          {DexValue::Ref(hello)})) == "hello",
          "toLowerCase() == \"hello\"");
    Check(vm.Call("Ljava/lang/String;", "equalsIgnoreCase",
                  {DexValue::Ref(hello), DexValue::Ref(vm.Str("hELLo"))}).i == 1,
          "equalsIgnoreCase(\"hELLo\")");
    Check(vm.Call("Ljava/lang/String;", "compareTo",
                  {DexValue::Ref(vm.Str("a")), DexValue::Ref(vm.Str("b"))}).i < 0,
          "compareTo(\"a\", \"b\") < 0");
    Check(vm.Call("Ljava/lang/String;", "compareTo",
                  {DexValue::Ref(vm.Str("ab")), DexValue::Ref(vm.Str("a"))}).i == 1,
          "compareTo(\"ab\", \"a\") == 1 (length difference)");
    Check(vm.Call("Ljava/lang/String;", "indexOf",
                  {DexValue::Ref(hello), DexValue::Int('l'), DexValue::Int(0)}).i == 2,
          "indexOf('l') == 2");
    Check(vm.Call("Ljava/lang/String;", "lastIndexOf",
                  {DexValue::Ref(hello), DexValue::Int('l'), DexValue::Int(5)}).i == 3,
          "lastIndexOf('l') == 3");

    // intern() must return the same object for equal contents.
    const DexValue a = vm.Call("Ljava/lang/String;", "intern", {DexValue::Ref(vm.Str("k"))});
    const DexValue b = vm.Call("Ljava/lang/String;", "intern", {DexValue::Ref(vm.Str("k"))});
    Check(a.l == b.l && a.l != nullptr, "intern() returns the same object");

    DexValue bytes = vm.Call("Ljava/lang/String;", "getBytes", {DexValue::Ref(hello)});
    auto* byte_array = reinterpret_cast<DexArray*>(bytes.l);
    Check(byte_array != nullptr && byte_array->length == 5, "getBytes().length == 5");
    DexValue chars = vm.Call("Ljava/lang/String;", "toCharArray", {DexValue::Ref(hello)});
    auto* char_array = reinterpret_cast<DexArray*>(chars.l);
    Check(char_array != nullptr && char_array->length == 5 &&
              reinterpret_cast<uint16_t*>(char_array + 1)[0] == 'H',
          "toCharArray()[0] == 'H'");

    // Non-ASCII: byte length and UTF-16 length differ, so the fast path must not
    // be used and indices must be code units.
    DexString* viet = vm.Str("Xin chào");
    Check(vm.Call("Ljava/lang/String;", "length", {DexValue::Ref(viet)}).i == 8,
          "length(\"Xin chào\") == 8 (UTF-16 units, not 9 bytes)");
    Check(vm.Call("Ljava/lang/String;", "charAt",
                  {DexValue::Ref(viet), DexValue::Int(6)}).i == 0x00E0,
          "charAt(6) == U+00E0");
    Check(vm.Utf8(vm.Call("Ljava/lang/String;", "substring",
                          {DexValue::Ref(viet), DexValue::Int(4), DexValue::Int(8)})) ==
              "chào",
          "substring on non-ASCII keeps the multi-byte char whole");

    // String has no Java instance fields, so AllocObject must still reserve room
    // for the native DexString payload the init* natives write.
    DexClass* string_class = vm.linker.FindClass("Ljava/lang/String;");
    DexObject* fresh = vm.linker.AllocObject(string_class);
    vm.Call("Ljava/lang/String;", "initCopy", {DexValue::Ref(fresh), DexValue::Ref(hello)});
    Check(vm.Utf8(DexValue::Ref(fresh)) == "Hello", "initCopy() fills the payload");

    DexClass* char_class = vm.linker.FindClass("[C");
    DexArray* src = vm.linker.AllocArray(char_class, 3);
    auto* src_data = reinterpret_cast<uint16_t*>(src + 1);
    src_data[0] = 'a';
    src_data[1] = 'b';
    src_data[2] = 'c';
    DexObject* from_chars = vm.linker.AllocObject(string_class);
    vm.Call("Ljava/lang/String;", "initChars",
            {DexValue::Ref(from_chars), DexValue::Ref(src), DexValue::Int(1),
             DexValue::Int(2)});
    Check(vm.Utf8(DexValue::Ref(from_chars)) == "bc", "initChars(offset=1, count=2)");

    vm.Call("Ljava/lang/String;", "initChars",
            {DexValue::Ref(vm.linker.AllocObject(string_class)), DexValue::Ref(src),
             DexValue::Int(2), DexValue::Int(5)});
    Check(vm.interp.HasPendingException(), "initChars past the end throws");
    vm.interp.ClearPendingException();
}

void TestObjectAndSystem(Vm& vm) {
    std::printf("-- java.lang.Object / System --\n");

    DexClass* point_class = vm.linker.FindClass("Lcom/foo/Point;");
    DexObject* p = vm.linker.AllocObject(point_class);
    kudroid::kuart::DexField* fx = point_class->FindInstanceField("x", "I");
    p->SetField<int32_t>(fx->offset_or_slot, 42);

    const DexValue cls = vm.Call("Ljava/lang/Object;", "getClass", {DexValue::Ref(p)});
    Check(cls.l != nullptr && vm.linker.ClassFromObject(cls.l) == point_class,
          "getClass() returns the Class object of the real class");

    const DexValue h1 = vm.Call("Ljava/lang/Object;", "hashCode", {DexValue::Ref(p)});
    const DexValue h2 = vm.Call("Ljava/lang/Object;", "hashCode", {DexValue::Ref(p)});
    Check(h1.i == h2.i, "hashCode() is stable across calls");

    const DexValue copy = vm.Call("Ljava/lang/Object;", "clone", {DexValue::Ref(p)});
    Check(copy.l != nullptr && copy.l != p &&
              copy.l->GetField<int32_t>(fx->offset_or_slot) == 42,
          "clone() copies field data into a new object");

    // Arrays clone element data, not just the header.
    DexArray* ints = vm.linker.AllocArray(vm.linker.FindClass("[I"), 3);
    reinterpret_cast<int32_t*>(ints + 1)[2] = 7;
    const DexValue arr_copy = vm.Call("Ljava/lang/Object;", "clone", {DexValue::Ref(ints)});
    auto* cloned = reinterpret_cast<DexArray*>(arr_copy.l);
    Check(cloned != nullptr && cloned != ints && cloned->length == 3 &&
              reinterpret_cast<int32_t*>(cloned + 1)[2] == 7,
          "clone() on an array copies elements");

    const int64_t millis = vm.Call("Ljava/lang/System;", "currentTimeMillis", {}).j;
    Check(millis > 1600000000000LL, "currentTimeMillis() is a plausible epoch value");
    const int64_t t1 = vm.Call("Ljava/lang/System;", "nanoTime", {}).j;
    const int64_t t2 = vm.Call("Ljava/lang/System;", "nanoTime", {}).j;
    Check(t2 >= t1, "nanoTime() is monotonic");

    Check(vm.Call("Ljava/lang/System;", "identityHashCode", {DexValue::Ref(nullptr)}).i == 0,
          "identityHashCode(null) == 0");

    DexArray* src = vm.linker.AllocArray(vm.linker.FindClass("[I"), 5);
    auto* src_data = reinterpret_cast<int32_t*>(src + 1);
    for (int i = 0; i < 5; ++i) src_data[i] = i;
    DexArray* dst = vm.linker.AllocArray(vm.linker.FindClass("[I"), 5);
    vm.Call("Ljava/lang/System;", "arraycopy",
            {DexValue::Ref(src), DexValue::Int(1), DexValue::Ref(dst), DexValue::Int(0),
             DexValue::Int(3)});
    auto* dst_data = reinterpret_cast<int32_t*>(dst + 1);
    Check(dst_data[0] == 1 && dst_data[1] == 2 && dst_data[2] == 3 && dst_data[3] == 0,
          "arraycopy() copies exactly the requested range");

    // Overlapping ranges inside one array are legal in Java: memmove, not memcpy.
    vm.Call("Ljava/lang/System;", "arraycopy",
            {DexValue::Ref(src), DexValue::Int(0), DexValue::Ref(src), DexValue::Int(1),
             DexValue::Int(4)});
    Check(src_data[0] == 0 && src_data[1] == 0 && src_data[2] == 1 && src_data[3] == 2 &&
              src_data[4] == 3,
          "arraycopy() handles overlap within one array");

    vm.Call("Ljava/lang/System;", "arraycopy",
            {DexValue::Ref(src), DexValue::Int(0), DexValue::Ref(dst), DexValue::Int(0),
             DexValue::Int(99)});
    Check(vm.interp.HasPendingException(), "arraycopy() out of range throws");
    vm.interp.ClearPendingException();

    Check(vm.Utf8(vm.Call("Ljava/lang/System;", "getProperty",
                          {DexValue::Ref(vm.Str("line.separator"))})) == "\n",
          "getProperty(\"line.separator\") == \"\\n\"");
    Check(vm.Call("Ljava/lang/System;", "getProperty",
                  {DexValue::Ref(vm.Str("no.such.property"))}).l == nullptr,
          "getProperty() of an unknown key is null");
}

void TestMathAndBits(Vm& vm) {
    std::printf("-- java.lang.Math / Double / Float --\n");

    Check(vm.Call("Ljava/lang/Math;", "sqrt", {DexValue::Double(16.0)}).d == 4.0,
          "sqrt(16) == 4");
    Check(vm.Call("Ljava/lang/Math;", "pow",
                  {DexValue::Double(2.0), DexValue::Double(10.0)}).d == 1024.0,
          "pow(2, 10) == 1024");
    Check(vm.Call("Ljava/lang/Math;", "floor", {DexValue::Double(-1.5)}).d == -2.0,
          "floor(-1.5) == -2");

    const int64_t bits = vm.Call("Ljava/lang/Double;", "doubleToLongBits",
                                {DexValue::Double(1.0)}).j;
    Check(bits == 0x3FF0000000000000LL, "doubleToLongBits(1.0) matches IEEE754");
    Check(vm.Call("Ljava/lang/Double;", "longBitsToDouble", {DexValue::Long(bits)}).d == 1.0,
          "longBitsToDouble() round-trips");
    const int32_t fbits = vm.Call("Ljava/lang/Float;", "floatToIntBits",
                                  {DexValue::Float(1.0f)}).i;
    Check(fbits == 0x3F800000, "floatToIntBits(1.0f) matches IEEE754");
    Check(vm.Call("Ljava/lang/Float;", "intBitsToFloat", {DexValue::Int(fbits)}).f == 1.0f,
          "intBitsToFloat() round-trips");

    // Double.toString must follow the Java spec, not printf's %g.
    auto to_string = [&vm](double d) {
        return vm.Utf8(vm.Call("Ljava/lang/Double;", "toString", {DexValue::Double(d)}));
    };
    Check(to_string(1.0) == "1.0", "toString(1.0) == \"1.0\" (always a decimal point)");
    Check(to_string(0.1) == "0.1", "toString(0.1) == \"0.1\" (shortest round-trip)");
    Check(to_string(-0.0) == "-0.0", "toString(-0.0) keeps the sign");
    Check(to_string(1.0 / 0.0) == "Infinity", "toString(inf) == \"Infinity\"");
    Check(to_string(0.0 / 0.0) == "NaN", "toString(NaN) == \"NaN\"");
    Check(to_string(1e7) == "1.0E7", "toString(1e7) uses scientific notation");
    Check(to_string(1e-3) == "0.001", "toString(1e-3) stays in plain notation");
    Check(to_string(1e-4) == "1.0E-4",
          "toString(1e-4) == \"1.0E-4\" (no zero-padded exponent)");
    Check(to_string(123456.789) == "123456.789", "toString(123456.789) round-trips");

    Check(vm.Call("Ljava/lang/Double;", "parseDouble",
                  {DexValue::Ref(vm.Str("2.5"))}).d == 2.5,
          "parseDouble(\"2.5\") == 2.5");
    Check(vm.Call("Ljava/lang/Double;", "parseDouble",
                  {DexValue::Ref(vm.Str(" 3.0f "))}).d == 3.0,
          "parseDouble() trims and accepts the 'f' suffix");
    vm.Call("Ljava/lang/Double;", "parseDouble", {DexValue::Ref(vm.Str("abc"))});
    Check(vm.interp.HasPendingException(), "parseDouble(\"abc\") throws");
    vm.interp.ClearPendingException();
}

void TestClassNatives(Vm& vm) {
    std::printf("-- java.lang.Class --\n");

    DexClass* point_class = vm.linker.FindClass("Lcom/foo/Point;");
    DexObject* point_obj = vm.linker.GetClassObject(point_class);
    Check(vm.Utf8(vm.Call("Ljava/lang/Class;", "getName", {DexValue::Ref(point_obj)})) ==
              "com.foo.Point",
          "getName() returns the dotted name");
    Check(vm.Call("Ljava/lang/Class;", "getModifiers", {DexValue::Ref(point_obj)}).i ==
              static_cast<int32_t>(point_class->access_flags),
          "getModifiers() returns the access flags");
    Check(vm.Call("Ljava/lang/Class;", "isArray", {DexValue::Ref(point_obj)}).i == 0,
          "isArray() is false for a normal class");

    DexObject* array_obj = vm.linker.GetClassObject(vm.linker.FindClass("[I"));
    Check(vm.Call("Ljava/lang/Class;", "isArray", {DexValue::Ref(array_obj)}).i == 1,
          "isArray() is true for [I");

    // Foo.class == Foo.class: the Class object must be cached per class.
    Check(vm.linker.GetClassObject(point_class) == point_obj,
          "GetClassObject() returns the cached object");
}

void TestMonitors(Vm& vm) {
    std::printf("-- monitors + wait/notify --\n");
    namespace Monitor = kudroid::kuart::Monitor;

    DexObject* lock = vm.linker.AllocObject(vm.linker.FindClass("Lcom/foo/Point;"));

    Monitor::Enter(lock);
    Check(lock->lock_count == 1, "Enter() takes the monitor");
    Monitor::Enter(lock);
    Check(lock->lock_count == 2, "Enter() is recursive");
    Check(Monitor::Exit(lock) && lock->lock_count == 1, "Exit() decrements");
    Check(Monitor::Exit(lock) && lock->lock_count == 0 && lock->lock_owner_tid == 0,
          "the last Exit() releases the monitor");
    Check(!Monitor::Exit(lock), "Exit() without owning fails");

    // wait/notify without owning the monitor is IllegalMonitorStateException.
    Check(!Monitor::Wait(lock, 0, 0), "Wait() without owning fails");
    Check(!Monitor::Notify(lock, true), "Notify() without owning fails");

    // A real handoff: the waiter must block until the notifier runs, and get the
    // monitor back at the same recursion depth.
    Monitor::Enter(lock);
    Monitor::Enter(lock);
    bool notified = false;
    std::thread notifier([&] {
        // Enter() blocks until Wait() releases the monitor, which proves Wait()
        // really gave it up rather than returning immediately.
        Monitor::Enter(lock);
        notified = true;
        Monitor::Notify(lock, true);
        Monitor::Exit(lock);
    });
    const bool waited = Monitor::Wait(lock, 5000, 0);
    Check(waited, "Wait() returns after being notified");
    Check(notified, "the notifier ran while the waiter was blocked");
    Check(lock->lock_count == 2, "Wait() restores the recursion depth");
    Monitor::Exit(lock);
    Monitor::Exit(lock);
    notifier.join();

    // A timed wait with nobody to notify must return instead of hanging.
    Monitor::Enter(lock);
    Check(Monitor::Wait(lock, 20, 0), "a timed Wait() returns on timeout");
    Check(lock->lock_count == 1, "the monitor is held again after the timeout");
    Monitor::Exit(lock);
}

void TestDirectByteBuffer(Vm& vm) {
    std::printf("-- java.nio.DirectByteBuffer --\n");

    const int64_t addr =
        vm.Call("Ljava/nio/DirectByteBuffer;", "nAllocate", {DexValue::Int(64)}).j;
    Check(addr != 0, "nAllocate(64) returns a real non-null address");

    // Fresh allocation is zero-filled.
    Check(vm.Call("Ljava/nio/DirectByteBuffer;", "nGet",
                  {DexValue::Long(addr), DexValue::Int(0)}).i == 0,
          "a fresh buffer reads back zero");

    vm.Call("Ljava/nio/DirectByteBuffer;", "nPut",
            {DexValue::Long(addr), DexValue::Int(5), DexValue::Int(0xAB)});
    Check(vm.Call("Ljava/nio/DirectByteBuffer;", "nGet",
                  {DexValue::Long(addr), DexValue::Int(5)}).i == -85,
          "nPut/nGet round-trips a byte (0xAB sign-extends to -85)");
    Check(vm.Call("Ljava/nio/DirectByteBuffer;", "nGet",
                  {DexValue::Long(addr), DexValue::Int(6)}).i == 0,
          "neighboring bytes are untouched");

    // Bulk path used by FMOD's e.get(f, ...) / track.write pump.
    DexClass* byte_array = vm.linker.FindClass("[B");
    DexArray* arr =
        byte_array != nullptr ? vm.linker.AllocArray(byte_array, 8) : nullptr;
    Check(arr != nullptr, "AllocArray([B, 8) works");
    if (arr != nullptr) {
        for (int i = 0; i < 8; ++i) arr->Set<int8_t>(i, static_cast<int8_t>(i + 1));
        vm.Call("Ljava/nio/DirectByteBuffer;", "nPutArray",
                {DexValue::Long(addr), DexValue::Int(10), DexValue::Ref(arr),
                 DexValue::Int(0), DexValue::Int(8)});
        for (int i = 0; i < 8; ++i) arr->Set<int8_t>(i, 0);
        vm.Call("Ljava/nio/DirectByteBuffer;", "nGetArray",
                {DexValue::Long(addr), DexValue::Int(10), DexValue::Ref(arr),
                 DexValue::Int(0), DexValue::Int(8)});
        bool bulk_ok = true;
        for (int i = 0; i < 8; ++i) bulk_ok &= arr->Get<int8_t>(i) == i + 1;
        Check(bulk_ok, "nPutArray/nGetArray round-trips 8 bytes");
        std::free(reinterpret_cast<void*>(static_cast<uintptr_t>(addr)));
    }
}

void TestAssetManager(Vm& vm) {
    std::printf("-- android.content.res.AssetManager --\n");

    DexValue dir = vm.Call("Landroid/content/res/AssetManager;", "nativeGetAssetsDir", {});
    Check(dir.l != nullptr, "nativeGetAssetsDir() returns a string (empty when unset)");
}

}  // namespace

int main() {
    std::printf("=== KuART libcore natives ===\n");

    DexBuilder builder;
    const std::vector<uint8_t> dex = builder.Build(BuildClasses());
    std::printf("synthetic DEX: %zu bytes\n", dex.size());

    Vm vm;
    std::string error;
    if (!vm.linker.AddDexFile(dex.data(), dex.size(), "libcore-test.dex", &error)) {
        std::printf("FATAL: AddDexFile failed: %s\n", error.c_str());
        return 1;
    }

    TestString(vm);
    TestObjectAndSystem(vm);
    TestMathAndBits(vm);
    TestClassNatives(vm);
    TestMonitors(vm);
    TestDirectByteBuffer(vm);
    TestAssetManager(vm);

    if (g_failures == 0) {
        std::printf("=== KuART libcore natives PASSED ===\n");
        return 0;
    }
    std::printf("=== KuART libcore natives FAILED (%d errors) ===\n", g_failures);
    return 1;
}
