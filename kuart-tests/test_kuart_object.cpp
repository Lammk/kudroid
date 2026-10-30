// Host test for the object-interaction opcode group: new/field/array/invoke/switch.
//
// Bytecode references class/field/method by DEX index, which settles only after
// the builder sorts each table. So the test builds TWICE: first with empty code
// to learn the indices, then with real bytecode.
#include "kudroid/kuart/DexClassLinker.h"
#include "kudroid/kuart/Interpreter.h"

#include <atomic>
#include <cstdio>
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
using kudroid::kuart::DexValue;

uint16_t Op12x(uint8_t op, uint8_t a, uint8_t b) {
    return static_cast<uint16_t>(op | (a << 8) | (b << 12));
}
uint16_t Op11x(uint8_t op, uint8_t a) { return static_cast<uint16_t>(op | (a << 8)); }
uint16_t Op11n(uint8_t op, uint8_t a, int8_t b) {
    return static_cast<uint16_t>(op | (a << 8) | ((b & 0xF) << 12));
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
void Op22b(std::vector<uint16_t>* code, uint8_t op, uint8_t a, uint8_t b, int8_t c) {
    code->push_back(static_cast<uint16_t>(op | (a << 8)));
    code->push_back(static_cast<uint16_t>((b & 0xFF) | ((c & 0xFF) << 8)));
}
// invoke 35c: op | argc<<12, idx, then packed 4-bit registers
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

constexpr uint8_t kOpConst4 = 0x12;
constexpr uint8_t kOpConst16 = 0x13;
constexpr uint8_t kOpReturnVoid = 0x0e;
constexpr uint8_t kOpReturn = 0x0f;
constexpr uint8_t kOpReturnWide = 0x10;
constexpr uint8_t kOpReturnObject = 0x11;
constexpr uint8_t kOpMoveResult = 0x0a;
constexpr uint8_t kOpMoveResultObject = 0x0c;
constexpr uint8_t kOpNewInstance = 0x22;
constexpr uint8_t kOpNewArray = 0x23;
constexpr uint8_t kOpArrayLength = 0x21;
constexpr uint8_t kOpInstanceOf = 0x20;
constexpr uint8_t kOpCheckCast = 0x1f;
constexpr uint8_t kOpAget = 0x44;
constexpr uint8_t kOpAput = 0x4b;
constexpr uint8_t kOpIget = 0x52;
constexpr uint8_t kOpIput = 0x59;
constexpr uint8_t kOpSget = 0x60;
constexpr uint8_t kOpSput = 0x67;
constexpr uint8_t kOpInvokeVirtual = 0x6e;
constexpr uint8_t kOpInvokeDirect = 0x70;
constexpr uint8_t kOpInvokeStatic = 0x71;
constexpr uint8_t kOpAddInt = 0x90;
constexpr uint8_t kOpAddIntLit8 = 0xd8;
constexpr uint8_t kOpConstString = 0x1a;

// Spec shared by both builds; only the code differs.
struct Specs {
    FieldSpec point_x{"x", "I", 0x1};
    FieldSpec point_y{"y", "I", 0x1};
    FieldSpec counter{"counter", "I", 0x9};

    MethodSpec point_ctor;
    MethodSpec point_get_x;
    MethodSpec point_sum;      // virtual, Point returns x+y
    MethodSpec point3d_sum;    // override, adds z
    FieldSpec point3d_z{"z", "I", 0x1};
    MethodSpec point3d_ctor;

    MethodSpec make_point;     // static: new Point, set fields, return object
    MethodSpec read_x;         // static: iget
    MethodSpec array_ops;      // static: new-array + aput + aget + array-length
    MethodSpec static_ops;     // static: sput then sget
    MethodSpec call_virtual;   // static: call sum() via Point param
    MethodSpec call_static;    // static: call addTwo()
    MethodSpec add_two;        // static helper
    MethodSpec bad_static_x;   // static: invoke-static on the instance method getX()
    MethodSpec instance_check; // static: instance-of
    MethodSpec npe_test;       // static: iget on null
    MethodSpec oob_test;       // static: out-of-range aget
    MethodSpec string_test;    // static: const-string returns object

    Specs() {
        point_ctor.name = "<init>";
        point_ctor.access_flags = 0x10001;

        point_get_x.name = "getX";
        point_get_x.return_type = "I";

        point_sum.name = "sum";
        point_sum.return_type = "I";

        point3d_ctor.name = "<init>";
        point3d_ctor.access_flags = 0x10001;

        point3d_sum.name = "sum";
        point3d_sum.return_type = "I";

        make_point.name = "makePoint";
        make_point.return_type = "LPoint;";
        make_point.params = {"I", "I"};
        make_point.access_flags = 0x9;

        read_x.name = "readX";
        read_x.return_type = "I";
        read_x.params = {"LPoint;"};
        read_x.access_flags = 0x9;

        array_ops.name = "arrayOps";
        array_ops.return_type = "I";
        array_ops.params = {"I"};
        array_ops.access_flags = 0x9;

        static_ops.name = "staticOps";
        static_ops.return_type = "I";
        static_ops.params = {"I"};
        static_ops.access_flags = 0x9;

        call_virtual.name = "callVirtual";
        call_virtual.return_type = "I";
        call_virtual.params = {"LPoint;"};
        call_virtual.access_flags = 0x9;

        add_two.name = "addTwo";
        add_two.return_type = "I";
        add_two.params = {"I", "I"};
        add_two.access_flags = 0x9;

        call_static.name = "callStatic";
        call_static.return_type = "I";
        call_static.access_flags = 0x9;

        // Deliberately invokes an instance method with invoke-static. The runtime
        // must reject the receiver-convention mismatch instead of shifting args.
        bad_static_x.name = "badStaticX";
        bad_static_x.return_type = "I";
        bad_static_x.params = {"LPoint;"};
        bad_static_x.access_flags = 0x9;

        instance_check.name = "instanceCheck";
        instance_check.return_type = "I";
        instance_check.params = {"LPoint;"};
        instance_check.access_flags = 0x9;

        npe_test.name = "npeTest";
        npe_test.return_type = "I";
        npe_test.access_flags = 0x9;

        oob_test.name = "oobTest";
        oob_test.return_type = "I";
        oob_test.access_flags = 0x9;

        string_test.name = "stringTest";
        string_test.return_type = "Ljava/lang/String;";
        string_test.access_flags = 0x9;
    }
};

std::vector<ClassSpec> BuildClasses(const Specs& s) {
    ClassSpec object;
    object.descriptor = "Ljava/lang/Object;";
    object.superclass = "";
    {
        MethodSpec ctor;
        ctor.name = "<init>";
        ctor.access_flags = 0x10001;
        ctor.code = {Op11x(kOpReturnVoid, 0)};
        ctor.registers_size = 1;
        ctor.ins_size = 1;
        object.direct_methods.push_back(ctor);
    }

    ClassSpec string;
    string.descriptor = "Ljava/lang/String;";
    string.superclass = "Ljava/lang/Object;";

    ClassSpec point;
    point.descriptor = "LPoint;";
    point.superclass = "Ljava/lang/Object;";
    point.instance_fields = {s.point_x, s.point_y};
    point.static_fields = {s.counter};
    point.direct_methods = {s.point_ctor};
    point.virtual_methods = {s.point_get_x, s.point_sum};

    ClassSpec point3d;
    point3d.descriptor = "LPoint3D;";
    point3d.superclass = "LPoint;";
    point3d.instance_fields = {s.point3d_z};
    point3d.direct_methods = {s.point3d_ctor};
    point3d.virtual_methods = {s.point3d_sum};

    ClassSpec t;
    t.descriptor = "LT;";
    t.superclass = "Ljava/lang/Object;";
    t.extra_strings = {"hello"};
    t.extra_types = {"[I"};
    t.direct_methods = {s.make_point,     s.read_x,       s.array_ops,
                        s.static_ops,    s.call_virtual, s.add_two,
                        s.call_static,   s.bad_static_x, s.instance_check,
                        s.npe_test,      s.oob_test,     s.string_test};

    return {object, string, point, point3d, t};
}

void ClassInitializationTests() {
    ClassSpec object;
    object.descriptor = "Ljava/lang/Object;";
    object.superclass = "";
    ClassSpec base;
    base.descriptor = "LInitBase;";
    FieldSpec count{"count", "I", 0x9};
    FieldSpec sentinel{"sentinel", "Ljava/lang/Object;", 0x9};
    base.static_fields = {count, sentinel};
    MethodSpec clinit;
    clinit.name = "<clinit>";
    clinit.access_flags = 0x10008;
    clinit.registers_size = 1;
    base.direct_methods = {clinit};
    ClassSpec child;
    child.descriptor = "LInitChild;";
    child.superclass = base.descriptor;
    ClassSpec grandchild;
    grandchild.descriptor = "LInitGrandchild;";
    grandchild.superclass = child.descriptor;
    ClassSpec own;
    own.descriptor = "LInitOwn;";
    own.superclass = base.descriptor;
    own.static_fields = {count};
    own.direct_methods = {clinit};

    DexBuilder indices;
    indices.Build({object, base, child, grandchild, own});
    auto increment = [&](const std::string& descriptor) {
        std::vector<uint16_t> code;
        auto field = static_cast<uint16_t>(indices.FieldIndexOf(descriptor, count));
        Op21c(&code, kOpSget, 0, field);
        Op22b(&code, kOpAddIntLit8, 0, 0, 1);
        Op21c(&code, kOpSput, 0, field);
        return code;
    };
    auto code = increment(base.descriptor);
    Op21c(&code, kOpNewInstance, 0,
          static_cast<uint16_t>(indices.TypeIndexOf(object.descriptor)));
    Op21c(&code, 0x69, 0,
          static_cast<uint16_t>(indices.FieldIndexOf(base.descriptor, sentinel)));
    code.push_back(kOpReturnVoid);
    base.direct_methods[0].code = code;
    code = increment(own.descriptor);
    code.push_back(kOpReturnVoid);
    own.direct_methods[0].code = code;
    DexBuilder builder;
    auto dex = builder.Build({object, base, child, grandchild, own});
    kudroid::kuart::DexClassLinker linker;
    std::string error;
    if (!linker.AddDexFile(dex.data(), dex.size(), "initialization.dex", &error)) {
        Check(false, "load class initialization regression: " + error);
        return;
    }
    kudroid::kuart::Interpreter interp(&linker);
    auto* b = linker.FindClass(base.descriptor.c_str());
    auto* c = linker.FindClass(child.descriptor.c_str());
    auto* g = linker.FindClass(grandchild.descriptor.c_str());
    auto* o = linker.FindClass(own.descriptor.c_str());
    if (b == nullptr || c == nullptr || g == nullptr || o == nullptr) {
        Check(false, "resolve class initialization hierarchy");
        return;
    }
    Check(interp.EnsureInitialized(b), "initialize base class");
    auto* count_field = b->FindStaticField("count", "I");
    auto* sentinel_field = b->FindStaticField("sentinel", "Ljava/lang/Object;");
    auto* original = b->static_values[sentinel_field->offset_or_slot].l;
    Check(original != nullptr, "base initializer creates its sentinel");
    Check(interp.EnsureInitialized(c) && interp.EnsureInitialized(g),
          "initialize descendants without declared initializers");
    Check(b->static_values[count_field->offset_or_slot].i == 1,
          "descendants do not replay the base initializer");
    Check(b->static_values[sentinel_field->offset_or_slot].l == original,
          "descendant initialization preserves static sentinel identity");
    Check(interp.EnsureInitialized(o) && interp.EnsureInitialized(o),
          "initialize a descendant with its own initializer twice");
    auto* own_count = o->FindStaticField("count", "I");
    Check(o->static_values[own_count->offset_or_slot].i == 1 &&
                  b->static_values[count_field->offset_or_slot].i == 1,
          "each declared initializer runs once");
    kudroid::kuart::DexClassLinker failed_linker;
    if (!failed_linker.AddDexFile(dex.data(), dex.size(), "failed-initialization.dex", &error)) {
        Check(false, "load failed initialization regression: " + error);
        return;
    }
    auto* failed_parent = failed_linker.FindClass(base.descriptor.c_str());
    auto* failed_child = failed_linker.FindClass(own.descriptor.c_str());
    if (failed_parent == nullptr || failed_child == nullptr) {
        Check(false, "resolve failed initialization hierarchy");
        return;
    }
    failed_parent->status = kudroid::kuart::DexClass::Status::kError;
    failed_parent->initialization_started.store(true);
    Check(!failed_linker.LinkClass(failed_parent) &&
                  failed_parent->status == kudroid::kuart::DexClass::Status::kError,
          "linking does not reset a failed class");
    kudroid::kuart::Interpreter isolated(&failed_linker);
    Check(!isolated.EnsureInitialized(failed_child) &&
                  failed_child->status == kudroid::kuart::DexClass::Status::kError &&
                  failed_parent->status == kudroid::kuart::DexClass::Status::kError,
          "parent initialization failure prevents child initialization");
}

}  // namespace

int main() {
    std::printf("=== KuART Interpreter 3b: object/field/array/invoke ===\n");
    ClassInitializationTests();

    // Pass 1: learn indices
    // "hello" and "[I" appear only in bytecode, so the builder cannot collect them;
    // declare via spec to get them into the tables.
    Specs probe;
    DexBuilder index_builder;
    index_builder.Build(BuildClasses(probe));

    const uint16_t kPointType = static_cast<uint16_t>(index_builder.TypeIndexOf("LPoint;"));
    const uint16_t kPoint3DType = static_cast<uint16_t>(index_builder.TypeIndexOf("LPoint3D;"));
    const uint16_t kIntArrayType = static_cast<uint16_t>(index_builder.TypeIndexOf("[I"));
    const uint16_t kFieldX =
        static_cast<uint16_t>(index_builder.FieldIndexOf("LPoint;", probe.point_x));
    const uint16_t kFieldY =
        static_cast<uint16_t>(index_builder.FieldIndexOf("LPoint;", probe.point_y));
    const uint16_t kFieldZ =
        static_cast<uint16_t>(index_builder.FieldIndexOf("LPoint3D;", probe.point3d_z));
    const uint16_t kFieldCounter =
        static_cast<uint16_t>(index_builder.FieldIndexOf("LPoint;", probe.counter));
    const uint16_t kMethodSum =
        static_cast<uint16_t>(index_builder.MethodIndexOf("LPoint;", probe.point_sum));
    const uint16_t kMethodAddTwo =
        static_cast<uint16_t>(index_builder.MethodIndexOf("LT;", probe.add_two));
    const uint16_t kMethodPointCtor =
        static_cast<uint16_t>(index_builder.MethodIndexOf("LPoint;", probe.point_ctor));
    const uint16_t kMethodGetX =
        static_cast<uint16_t>(index_builder.MethodIndexOf("LPoint;", probe.point_get_x));
    const uint16_t kStringHello =
        static_cast<uint16_t>(index_builder.StringIndexOf("hello"));

    // Pass 2: fill in real bytecode
    Specs s;

    // Point.<init>()V  { return; }
    s.point_ctor.code = {Op11x(kOpReturnVoid, 0)};
    s.point_ctor.registers_size = 1;
    s.point_ctor.ins_size = 1;

    // Point.getX()I  { return this.x; }  v0 result, v1 = this
    {
        std::vector<uint16_t> c;
        Op22c(&c, kOpIget, 0, 1, kFieldX);
        c.push_back(Op11x(kOpReturn, 0));
        s.point_get_x.code = c;
        s.point_get_x.registers_size = 2;
        s.point_get_x.ins_size = 1;
    }
    // Point.sum()I  { return this.x + this.y; }  v0,v1 scratch, v2 = this
    {
        std::vector<uint16_t> c;
        Op22c(&c, kOpIget, 0, 2, kFieldX);
        Op22c(&c, kOpIget, 1, 2, kFieldY);
        Op23x(&c, kOpAddInt, 0, 0, 1);
        c.push_back(Op11x(kOpReturn, 0));
        s.point_sum.code = c;
        s.point_sum.registers_size = 3;
        s.point_sum.ins_size = 1;
    }
    // Point3D.<init>()V
    s.point3d_ctor.code = {Op11x(kOpReturnVoid, 0)};
    s.point3d_ctor.registers_size = 1;
    s.point3d_ctor.ins_size = 1;

    // Point3D.sum()I  { return this.x + this.y + this.z; } — override
    {
        std::vector<uint16_t> c;
        Op22c(&c, kOpIget, 0, 2, kFieldX);
        Op22c(&c, kOpIget, 1, 2, kFieldY);
        Op23x(&c, kOpAddInt, 0, 0, 1);
        Op22c(&c, kOpIget, 1, 2, kFieldZ);
        Op23x(&c, kOpAddInt, 0, 0, 1);
        c.push_back(Op11x(kOpReturn, 0));
        s.point3d_sum.code = c;
        s.point3d_sum.registers_size = 3;
        s.point3d_sum.ins_size = 1;
    }

    // T.makePoint(int x, int y) { Point p = new Point(); p.x = x; p.y = y; return p; }
    // v0 = p, v1 = x, v2 = y  (regs=3, ins=2, params in v1,v2)
    {
        std::vector<uint16_t> c;
        Op21c(&c, kOpNewInstance, 0, kPointType);
        Op35c(&c, kOpInvokeDirect, kMethodPointCtor, {0});
        Op22c(&c, kOpIput, 1, 0, kFieldX);
        Op22c(&c, kOpIput, 2, 0, kFieldY);
        c.push_back(Op11x(kOpReturnObject, 0));
        s.make_point.code = c;
        s.make_point.registers_size = 3;
        s.make_point.ins_size = 2;
    }
    // T.readX(Point p) { return p.x; }
    {
        std::vector<uint16_t> c;
        Op22c(&c, kOpIget, 0, 1, kFieldX);
        c.push_back(Op11x(kOpReturn, 0));
        s.read_x.code = c;
        s.read_x.registers_size = 2;
        s.read_x.ins_size = 1;
    }
    // T.arrayOps(int n) { int[] a = new int[3]; a[1] = n; return a[1] + a.length; }
    // v0 = a, v1 scratch, v2 scratch, v3 = n
    {
        std::vector<uint16_t> c;
        c.push_back(Op11n(kOpConst4, 1, 3));       // v1 = 3
        Op22c(&c, kOpNewArray, 0, 1, kIntArrayType);
        c.push_back(Op11n(kOpConst4, 1, 1));       // v1 = 1 (index)
        Op23x(&c, kOpAput, 3, 0, 1);               // a[1] = n
        Op23x(&c, kOpAget, 2, 0, 1);               // v2 = a[1]
        c.push_back(Op12x(kOpArrayLength, 1, 0));  // v1 = a.length
        Op23x(&c, kOpAddInt, 0, 2, 1);
        c.push_back(Op11x(kOpReturn, 0));
        s.array_ops.code = c;
        s.array_ops.registers_size = 4;
        s.array_ops.ins_size = 1;
    }
    // T.staticOps(int n) { Point.counter = n; return Point.counter + 1; }
    {
        std::vector<uint16_t> c;
        Op21c(&c, kOpSput, 1, kFieldCounter);
        Op21c(&c, kOpSget, 0, kFieldCounter);
        Op22b(&c, kOpAddIntLit8, 0, 0, 1);
        c.push_back(Op11x(kOpReturn, 0));
        s.static_ops.code = c;
        s.static_ops.registers_size = 2;
        s.static_ops.ins_size = 1;
    }
    // T.callVirtual(Point p) { return p.sum(); }
    {
        std::vector<uint16_t> c;
        Op35c(&c, kOpInvokeVirtual, kMethodSum, {1});
        c.push_back(Op11x(kOpMoveResult, 0));
        c.push_back(Op11x(kOpReturn, 0));
        s.call_virtual.code = c;
        s.call_virtual.registers_size = 2;
        s.call_virtual.ins_size = 1;
    }
    // T.addTwo(int a, int b) { return a + b; }
    {
        std::vector<uint16_t> c;
        Op23x(&c, kOpAddInt, 0, 1, 2);
        c.push_back(Op11x(kOpReturn, 0));
        s.add_two.code = c;
        s.add_two.registers_size = 3;
        s.add_two.ins_size = 2;
    }
    // T.callStatic() { return addTwo(20, 22); }
    {
        std::vector<uint16_t> c;
        Op21c(&c, kOpConst16, 1, 20);
        Op21c(&c, kOpConst16, 2, 22);
        Op35c(&c, kOpInvokeStatic, kMethodAddTwo, {1, 2});
        c.push_back(Op11x(kOpMoveResult, 0));
        c.push_back(Op11x(kOpReturn, 0));
        s.call_static.code = c;
        s.call_static.registers_size = 3;
        s.call_static.ins_size = 0;
    }
    // T.badStaticX(Point p) { return Point.getX(p); }  -- static opcode, instance method
    {
        std::vector<uint16_t> c;
        Op35c(&c, kOpInvokeStatic, kMethodGetX, {1});
        c.push_back(Op11x(kOpMoveResult, 0));
        c.push_back(Op11x(kOpReturn, 0));
        s.bad_static_x.code = c;
        s.bad_static_x.registers_size = 2;
        s.bad_static_x.ins_size = 1;
    }
    // T.instanceCheck(Point p) { return (p instanceof Point3D) ? 1 : 0; }
    {
        std::vector<uint16_t> c;
        Op22c(&c, kOpInstanceOf, 0, 1, kPoint3DType);
        c.push_back(Op11x(kOpReturn, 0));
        s.instance_check.code = c;
        s.instance_check.registers_size = 2;
        s.instance_check.ins_size = 1;
    }
    // T.npeTest() { Point p = null; return p.x; }
    {
        std::vector<uint16_t> c;
        c.push_back(Op11n(kOpConst4, 1, 0));  // v1 = null
        Op22c(&c, kOpIget, 0, 1, kFieldX);
        c.push_back(Op11x(kOpReturn, 0));
        s.npe_test.code = c;
        s.npe_test.registers_size = 2;
        s.npe_test.ins_size = 0;
    }
    // T.oobTest() { int[] a = new int[2]; return a[5]; }
    {
        std::vector<uint16_t> c;
        c.push_back(Op11n(kOpConst4, 1, 2));
        Op22c(&c, kOpNewArray, 0, 1, kIntArrayType);
        c.push_back(Op11n(kOpConst4, 1, 5));
        Op23x(&c, kOpAget, 0, 0, 1);
        c.push_back(Op11x(kOpReturn, 0));
        s.oob_test.code = c;
        s.oob_test.registers_size = 2;
        s.oob_test.ins_size = 0;
    }
    // T.stringTest() { return "hello"; }
    {
        std::vector<uint16_t> c;
        Op21c(&c, kOpConstString, 0, kStringHello);
        c.push_back(Op11x(kOpReturnObject, 0));
        s.string_test.code = c;
        s.string_test.registers_size = 1;
        s.string_test.ins_size = 0;
    }

    DexBuilder builder;
    const std::vector<uint8_t> dex = builder.Build(BuildClasses(s));
    std::printf("DEX synthetic: %zu bytes\n", dex.size());

    // Pass-2 indices must match pass 1, or bytecode points at the wrong entity.
Check(builder.TypeIndexOf("LPoint;") == kPointType, "index  n  nh gi a hai l t build");

    kudroid::kuart::DexClassLinker linker;
    std::string error;
    if (!linker.AddDexFile(dex.data(), dex.size(), "test.dex", &error)) {
        std::printf("  FAIL AddDexFile: %s\n=== FAILED ===\n", error.c_str());
        return 1;
    }

    // Concurrent first lookups of one descriptor must converge on one DexClass.
    {
        constexpr int kThreads = 8;
        std::vector<kudroid::kuart::DexClass*> found(kThreads, nullptr);
        std::atomic<int> ready{0};
        std::atomic<bool> go{false};
        std::vector<std::thread> threads;
        threads.reserve(kThreads);
        for (int i = 0; i < kThreads; ++i) {
            threads.emplace_back([&, i] {
                ready.fetch_add(1);
                while (!go.load(std::memory_order_acquire)) std::this_thread::yield();
                found[i] = linker.FindClass("LPoint3D;");
            });
        }
        while (ready.load() != kThreads) std::this_thread::yield();
        go.store(true, std::memory_order_release);
        for (auto& th : threads) th.join();
        bool same = found[0] != nullptr;
        for (int i = 1; i < kThreads; ++i) same = same && found[i] == found[0];
        Check(same, "concurrent FindClass returns one class instance");
    }

    kudroid::kuart::DexClass* t = linker.FindClass("LT;");
    kudroid::kuart::DexClass* point = linker.FindClass("LPoint;");
    kudroid::kuart::DexClass* point3d = linker.FindClass("LPoint3D;");
    if (t == nullptr || point == nullptr || point3d == nullptr) {
        std::printf("  FAIL FindClass: %s\n=== FAILED ===\n", linker.last_error().c_str());
        return 1;
    }

    kudroid::kuart::Interpreter interp(&linker);

    const auto call = [&](const char* name, const char* sig,
                          std::vector<DexValue> args) -> DexValue {
        interp.ClearPendingException();
        kudroid::kuart::DexMethod* m = t->FindDirectMethod(name, sig);
        if (m == nullptr) {
std::printf("  FAIL not found %s%s\n", name, sig);
            ++g_failures;
            return DexValue();
        }
        return interp.Execute(m, args.data(), args.size());
    };

    // ── new-instance + iput + iget ──
    const DexValue p = call("makePoint", "(II)LPoint;", {DexValue::Int(7), DexValue::Int(35)});
Check(p.l != nullptr, "makePoint tr  object kh c null");
Check(p.l != nullptr && p.l->clazz == point, "object c  class l  Point");
Check(call("readX", "(LPoint;)I", {p}).i == 7, "readX  c  ng field   iput");
    Check(call("readX", "(LPoint;)I", {p}).i == 7, "cached instance field retains its value");
    const DexValue unrelated = DexValue::Ref(linker.AllocObject(t));
    call("readX", "(LPoint;)I", {unrelated});
    Check(interp.HasPendingException(), "cached field rejects an unrelated receiver class");
    Check(interp.HasPendingException() &&
              interp.DescribePendingException().find("NoSuchFieldError") != std::string::npos,
          "cached field preserves receiver validation errors");

    // invoke-virtual + correct dispatch
    Check(call("callVirtual", "(LPoint;)I", {p}).i == 42, "callVirtual(Point) == 7+35 == 42");

    // Point3D overrides sum(): same bytecode call, different result.
    kudroid::kuart::DexObject* p3 = linker.AllocObject(point3d);
    Check(p3 != nullptr, "AllocObject(Point3D)");
    if (p3 != nullptr) {
        kudroid::kuart::DexField* fx = point3d->FindInstanceField("x", "I");
        kudroid::kuart::DexField* fy = point3d->FindInstanceField("y", "I");
        kudroid::kuart::DexField* fz = point3d->FindInstanceField("z", "I");
Check(fx != nullptr && fy != nullptr && fz != nullptr, "Point3D th y c  x,y k  th a v  z");
        if (fx != nullptr && fy != nullptr && fz != nullptr) {
            p3->SetField<int32_t>(fx->offset_or_slot, 1);
            p3->SetField<int32_t>(fy->offset_or_slot, 2);
            p3->SetField<int32_t>(fz->offset_or_slot, 4);
            Check(call("callVirtual", "(LPoint;)I", {DexValue::Ref(p3)}).i == 7,
"callVirtual(Point3D) == 1+2+4 == 7 (g i b n override)");
        }
    }

    // ── invoke-static ──
    Check(call("callStatic", "()I", {}).i == 42, "callStatic() == 20+22 == 42");

    // invoke-static must not run an instance method with shifted arguments.
    {
        interp.ClearPendingException();
        kudroid::kuart::DexMethod* m = t->FindDirectMethod("badStaticX", "(LPoint;)I");
        if (m == nullptr) {
            Check(false, "badStaticX resolved");
        } else {
            interp.Execute(m, &p, 1);
            Check(interp.HasPendingException(), "invoke-static on an instance method throws");
            Check(interp.last_error().find("NoSuchMethodError") != std::string::npos,
                  "the failure is a NoSuchMethodError: " + interp.last_error());
            interp.ClearPendingException();
        }
    }

    // array
    Check(call("arrayOps", "(I)I", {DexValue::Int(10)}).i == 13,
          "arrayOps(10) == a[1] + a.length == 10+3 == 13");

    // ── field static ──
    Check(call("staticOps", "(I)I", {DexValue::Int(99)}).i == 100,
"staticOps(99) == 100 (sput r i sget)");
    // Static value must persist after the method returns.
    kudroid::kuart::DexField* counter = point->FindStaticField("counter", "I");
    Check(counter != nullptr && point->static_values[counter->offset_or_slot].i == 99,
"field static gi  gi  tr  sau khi method tr  v ");

    // ── instance-of ──
    Check(call("instanceCheck", "(LPoint;)I", {p}).i == 0,
          "Point instanceof Point3D == false");
    if (p3 != nullptr) {
        Check(call("instanceCheck", "(LPoint;)I", {DexValue::Ref(p3)}).i == 1,
              "Point3D instanceof Point3D == true");
    }

    // ── const-string ──
    {
        const DexValue str = call("stringTest", "()Ljava/lang/String;", {});
        auto* ds = static_cast<kudroid::kuart::DexString*>(str.l);
        Check(ds != nullptr && ds->utf8 != nullptr && std::strcmp(ds->utf8, "hello") == 0,
"const-string tr  chu i \"hello\"");
        // Interning: repeat call must return the SAME object.
        const DexValue str2 = call("stringTest", "()Ljava/lang/String;", {});
Check(str.l == str2.l, "chu i h ng  c intern (c ng con tr )");
    }

    // ── exception ──
    {
        interp.ClearPendingException();
        kudroid::kuart::DexMethod* m = t->FindDirectMethod("npeTest", "()I");
        interp.Execute(m, nullptr, 0);
Check(interp.HasPendingException(), "iget tr n null n m exception");
        Check(interp.last_error().find("NullPointerException") != std::string::npos,
"exception l  NullPointerException");
        interp.ClearPendingException();
    }
    {
        interp.ClearPendingException();
        kudroid::kuart::DexMethod* m = t->FindDirectMethod("oobTest", "()I");
        interp.Execute(m, nullptr, 0);
Check(interp.HasPendingException(), "aget ngo i d i n m exception");
        Check(interp.last_error().find("ArrayIndexOutOfBounds") != std::string::npos,
"exception l  ArrayIndexOutOfBoundsException");
        interp.ClearPendingException();
    }

    std::printf("heap: %zu bytes, %zu class\n", linker.heap().BytesAllocated(),
                linker.NumLoadedClasses());

    // Resolution results must stay inside their runtime across heap reuse.
    bool isolated = true;
    for (int run = 0; run < 8; ++run) {
        kudroid::kuart::DexClassLinker next_linker;
        isolated = isolated && next_linker.AddDexFile(dex.data(), dex.size(), "next.dex", &error);
        kudroid::kuart::Interpreter next_interp(&next_linker);
        auto* owner = next_linker.FindClass("LT;");
        auto* make = owner != nullptr ? owner->FindDirectMethod("makePoint", "(II)LPoint;") : nullptr;
        DexValue values[] = {DexValue::Int(run + 10), DexValue::Int(run + 20)};
        const DexValue created = next_interp.Execute(make, values, 2);
        isolated = isolated && !next_interp.HasPendingException() && created.l != nullptr
                && created.l->clazz == next_linker.FindClass("LPoint;");
        auto* read = owner != nullptr ? owner->FindDirectMethod("readX", "(LPoint;)I") : nullptr;
        const DexValue x = next_interp.Execute(read, &created, 1);
        isolated = isolated && !next_interp.HasPendingException() && x.i == run + 10;
        next_interp.ClearPendingException();
    }
    Check(isolated, "cached types, fields and methods remain isolated after runtime recreation");
    Check(call("readX", "(LPoint;)I", {p}).i == 7,
          "another runtime shutdown cannot change the original runtime field result");

    if (g_failures == 0) {
        std::printf("=== KuART Interpreter 3b test PASSED ===\n");
        return 0;
    }
std::printf("=== KuART Interpreter 3b test FAILED (%d error) ===\n", g_failures);
    return 1;
}
