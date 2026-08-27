// Host test for nh m opcode object interaction: new/field/array/invoke/switch.
//
// Bytecode tham chi u class/field/method b ng INDEX trong DEX, m  index ch
// ch t sau khi builder sort xong m i b ng. N n test build HAI L N: l n  u v i
// code r ng   h c index, l n hai v i bytecode th t. C ng b  spec th  index
// gi ng nhau n n c ch n y tin  c.
#include "kudroid/kuart/DexClassLinker.h"
#include "kudroid/kuart/Interpreter.h"

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
// invoke 35c: op | argc<<12, idx, r i c c register 4-bit  ng g i
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

// Spec d ng chung cho c  hai l n build; ch  ph n code kh c nhau.
struct Specs {
    FieldSpec point_x{"x", "I", 0x1};
    FieldSpec point_y{"y", "I", 0x1};
    FieldSpec counter{"counter", "I", 0x9};

    MethodSpec point_ctor;
    MethodSpec point_get_x;
    MethodSpec point_sum;      // virtual, Point tr  x+y
    MethodSpec point3d_sum;    // override, c ng th m z
    FieldSpec point3d_z{"z", "I", 0x1};
    MethodSpec point3d_ctor;

    MethodSpec make_point;     // static: new Point, set field, tr  object
    MethodSpec read_x;         // static: iget
    MethodSpec array_ops;      // static: new-array + aput + aget + array-length
    MethodSpec static_ops;     // static: sput r i sget
    MethodSpec call_virtual;   // static: g i sum() qua tham s  Point
    MethodSpec call_static;    // static: g i addTwo()
    MethodSpec add_two;        // static helper
    MethodSpec instance_check; // static: instance-of
    MethodSpec npe_test;       // static: iget tr n null
    MethodSpec oob_test;       // static: aget ngo i d i
    MethodSpec string_test;    // static: const-string tr  object

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
                        s.call_static,   s.instance_check, s.npe_test,
                        s.oob_test,      s.string_test};

    return {object, string, point, point3d, t};
}

}  // namespace

int main() {
    std::printf("=== KuART Interpreter 3b: object/field/array/invoke ===\n");

    // L t 1: h c index
    // "hello" v  "[I" ch  xu t hi n trong bytecode (const-string, new-array) n n
    // builder kh ng t  thu th p  c   ph i khai b o qua spec   ch ng v o b ng.
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
    const uint16_t kStringHello =
        static_cast<uint16_t>(index_builder.StringIndexOf("hello"));

    // L t 2:  i n bytecode th t
    Specs s;

    // Point.<init>()V  { return; }
    s.point_ctor.code = {Op11x(kOpReturnVoid, 0)};
    s.point_ctor.registers_size = 1;
    s.point_ctor.ins_size = 1;

    // Point.getX()I  { return this.x; }  v0 kq, v1 = this
    {
        std::vector<uint16_t> c;
        Op22c(&c, kOpIget, 0, 1, kFieldX);
        c.push_back(Op11x(kOpReturn, 0));
        s.point_get_x.code = c;
        s.point_get_x.registers_size = 2;
        s.point_get_x.ins_size = 1;
    }
    // Point.sum()I  { return this.x + this.y; }  v0,v1 t m, v2 = this
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
    // v0 = p, v1 = x, v2 = y  (regs=3, ins=2   tham s    v1,v2)
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
    // v0 = a, v1 t m, v2 t m, v3 = n
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

    // Index c a l t 2 ph i kh p l t 1, n u kh ng bytecode tr  sai entity.
Check(builder.TypeIndexOf("LPoint;") == kPointType, "index  n  nh gi a hai l t build");

    kudroid::kuart::DexClassLinker linker;
    std::string error;
    if (!linker.AddDexFile(dex.data(), dex.size(), "test.dex", &error)) {
        std::printf("  FAIL AddDexFile: %s\n=== FAILED ===\n", error.c_str());
        return 1;
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

    // invoke-virtual + dispatch  ng
    Check(call("callVirtual", "(LPoint;)I", {p}).i == 42, "callVirtual(Point) == 7+35 == 42");

    // Point3D override sum()   c ng bytecode g i, kh c k t qu .
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

    // m ng
    Check(call("arrayOps", "(I)I", {DexValue::Int(10)}).i == 13,
          "arrayOps(10) == a[1] + a.length == 10+3 == 13");

    // ── field static ──
    Check(call("staticOps", "(I)I", {DexValue::Int(99)}).i == 100,
"staticOps(99) == 100 (sput r i sget)");
    // Gi  tr  static ph i gi  l i sau khi method finished.
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
        // Interning: g i l i ph i ra C NG object.
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

    if (g_failures == 0) {
        std::printf("=== KuART Interpreter 3b test PASSED ===\n");
        return 0;
    }
std::printf("=== KuART Interpreter 3b test FAILED (%d error) ===\n", g_failures);
    return 1;
}
