// Host test for KuART b c 5: try/catch table + auto-executing <clinit>.
//
// V n build hai l t (l t 1 h c index, l t 2  i n bytecode th t) nh  c c test
// KuART kh c. Offset trong bytecode t nh theo CODE UNIT    m sai m t  n v  l
// nh y v o gi a instruction, n n m i kh i code d i  y c  ch  th ch pc.
#include "kudroid/kuart/DexClassLinker.h"
#include "kudroid/kuart/Interpreter.h"

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
uint16_t Op11n(uint8_t op, uint8_t a, int8_t b) {
    return static_cast<uint16_t>(op | (a << 8) | ((b & 0xF) << 12));
}
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
void Op22b(std::vector<uint16_t>* code, uint8_t op, uint8_t a, uint8_t b, int8_t c) {
    code->push_back(static_cast<uint16_t>(op | (a << 8)));
    code->push_back(static_cast<uint16_t>((b & 0xFF) | ((c & 0xFF) << 8)));
}
void Op23x(std::vector<uint16_t>* code, uint8_t op, uint8_t a, uint8_t b, uint8_t c) {
    code->push_back(static_cast<uint16_t>(op | (a << 8)));
    code->push_back(static_cast<uint16_t>(b | (c << 8)));
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

constexpr uint8_t kOpConst4 = 0x12;
constexpr uint8_t kOpConst16 = 0x13;
constexpr uint8_t kOpMoveException = 0x0d;
constexpr uint8_t kOpReturnVoid = 0x0e;
constexpr uint8_t kOpReturn = 0x0f;
constexpr uint8_t kOpMoveResult = 0x0a;
constexpr uint8_t kOpNewInstance = 0x22;
constexpr uint8_t kOpInstanceOf = 0x20;
constexpr uint8_t kOpThrow = 0x27;
constexpr uint8_t kOpSget = 0x60;
constexpr uint8_t kOpSput = 0x67;
constexpr uint8_t kOpInvokeDirect = 0x70;
constexpr uint8_t kOpInvokeStatic = 0x71;
constexpr uint8_t kOpDivInt = 0x93;
constexpr uint8_t kOpAddIntLit8 = 0xd8;

constexpr uint32_t kAccPublicStatic = 0x1 | 0x8;
constexpr uint32_t kAccStaticCtor = 0x8 | 0x10000;  // ACC_STATIC | ACC_CONSTRUCTOR

struct Specs {
    FieldSpec c_init{"init", "I", 0x9};
    FieldSpec counter_n{"n", "I", 0x9};
    FieldSpec bad_x{"x", "I", 0x9};

    MethodSpec object_ctor;
    MethodSpec throwable_ctor;
    MethodSpec myex_ctor;
    MethodSpec c_clinit;
    MethodSpec bad_clinit;

    MethodSpec catch_arith;     // try div → catch ArithmeticException
    MethodSpec catch_two;       // hai handler, handler th  hai kh p
    MethodSpec catch_all;       // catch-all (finally)
    MethodSpec catch_check;     // move-exception r i instance-of
    MethodSpec no_match;        // handler kh ng kh p   truy n l n
    MethodSpec uncaught;        // kh ng c  try
    MethodSpec thrower;         // n m   caller b t
    MethodSpec caller_catches;  // b t exception c a callee
    MethodSpec read_c;          // sget   k ch ho t <clinit> c a LC
    MethodSpec read_counter;
    MethodSpec read_bad;        // <clinit> n m exception

    Specs() {
        object_ctor.name = "<init>";
        object_ctor.access_flags = 0x10001;
        throwable_ctor.name = "<init>";
        throwable_ctor.access_flags = 0x10001;
        myex_ctor.name = "<init>";
        myex_ctor.access_flags = 0x10001;

        c_clinit.name = "<clinit>";
        c_clinit.access_flags = kAccStaticCtor;
        bad_clinit.name = "<clinit>";
        bad_clinit.access_flags = kAccStaticCtor;

        const auto make_static = [](MethodSpec* m, const char* name, const char* ret,
                                    std::vector<std::string> params) {
            m->name = name;
            m->return_type = ret;
            m->params = std::move(params);
            m->access_flags = kAccPublicStatic;
        };
        make_static(&catch_arith, "catchArith", "I", {"I", "I"});
        make_static(&catch_two, "catchTwo", "I", {"I", "I"});
        make_static(&catch_all, "catchAll", "I", {});
        make_static(&catch_check, "catchCheck", "I", {});
        make_static(&no_match, "noMatch", "I", {"I", "I"});
        make_static(&uncaught, "uncaught", "I", {});
        make_static(&thrower, "thrower", "V", {});
        make_static(&caller_catches, "callerCatches", "I", {});
        make_static(&read_c, "readC", "I", {});
        make_static(&read_counter, "readCounter", "I", {});
        make_static(&read_bad, "readBad", "I", {});
    }
};

std::vector<ClassSpec> BuildClasses(const Specs& s) {
    ClassSpec object;
    object.descriptor = "Ljava/lang/Object;";
    object.superclass = "";
    object.direct_methods = {s.object_ctor};

    ClassSpec string;
    string.descriptor = "Ljava/lang/String;";

    ClassSpec throwable;
    throwable.descriptor = "Ljava/lang/Throwable;";
    throwable.direct_methods = {s.throwable_ctor};

    ClassSpec exception;
    exception.descriptor = "Ljava/lang/Exception;";
    exception.superclass = "Ljava/lang/Throwable;";

    ClassSpec runtime_ex;
    runtime_ex.descriptor = "Ljava/lang/RuntimeException;";
    runtime_ex.superclass = "Ljava/lang/Exception;";

    ClassSpec arith;
    arith.descriptor = "Ljava/lang/ArithmeticException;";
    arith.superclass = "Ljava/lang/RuntimeException;";

    ClassSpec npe;
    npe.descriptor = "Ljava/lang/NullPointerException;";
    npe.superclass = "Ljava/lang/RuntimeException;";

    ClassSpec myex;
    myex.descriptor = "LMyEx;";
    myex.superclass = "Ljava/lang/RuntimeException;";
    myex.direct_methods = {s.myex_ctor};

    ClassSpec counter;
    counter.descriptor = "LCounter;";
    counter.static_fields = {s.counter_n};

    ClassSpec c;
    c.descriptor = "LC;";
    c.static_fields = {s.c_init};
    c.direct_methods = {s.c_clinit};

    ClassSpec bad;
    bad.descriptor = "LBad;";
    bad.static_fields = {s.bad_x};
    bad.direct_methods = {s.bad_clinit};

    ClassSpec e;
    e.descriptor = "LE;";
    e.direct_methods = {s.catch_arith,   s.catch_two,      s.catch_all,
                        s.catch_check,   s.no_match,       s.uncaught,
                        s.thrower,       s.caller_catches, s.read_c,
                        s.read_counter,  s.read_bad};

    return {object, string, throwable, exception, runtime_ex, arith,
            npe,    myex,   counter,   c,         bad,        e};
}

}  // namespace

int main() {
std::printf("=== KuART b c 5: try/catch + <clinit> ===\n");

    Specs probe;
    DexBuilder index_builder;
    index_builder.Build(BuildClasses(probe));

    const uint16_t kTypeMyEx = static_cast<uint16_t>(index_builder.TypeIndexOf("LMyEx;"));
    const uint16_t kMethodMyExCtor =
        static_cast<uint16_t>(index_builder.MethodIndexOf("LMyEx;", probe.myex_ctor));
    const uint16_t kMethodThrower =
        static_cast<uint16_t>(index_builder.MethodIndexOf("LE;", probe.thrower));
    const uint16_t kFieldCInit =
        static_cast<uint16_t>(index_builder.FieldIndexOf("LC;", probe.c_init));
    const uint16_t kFieldCounterN =
        static_cast<uint16_t>(index_builder.FieldIndexOf("LCounter;", probe.counter_n));
    const uint16_t kFieldBadX =
        static_cast<uint16_t>(index_builder.FieldIndexOf("LBad;", probe.bad_x));

    Specs s;
    s.object_ctor.code = {Op11x(kOpReturnVoid, 0)};
    s.object_ctor.registers_size = 1;
    s.object_ctor.ins_size = 1;
    s.throwable_ctor.code = {Op11x(kOpReturnVoid, 0)};
    s.throwable_ctor.registers_size = 1;
    s.throwable_ctor.ins_size = 1;
    s.myex_ctor.code = {Op11x(kOpReturnVoid, 0)};
    s.myex_ctor.registers_size = 1;
    s.myex_ctor.ins_size = 1;

    // LC.<clinit>: init = 42; Counter.n = Counter.n + 1
    // T ng Counter.n    m s  l n <clinit> th c s  ch y.
    {
        std::vector<uint16_t> c;
        Op21s(&c, kOpConst16, 0, 42);          // pc 0
        Op21c(&c, kOpSput, 0, kFieldCInit);    // pc 2
        Op21c(&c, kOpSget, 0, kFieldCounterN); // pc 4
        Op22b(&c, kOpAddIntLit8, 0, 0, 1);     // pc 6
        Op21c(&c, kOpSput, 0, kFieldCounterN); // pc 8
        c.push_back(Op11x(kOpReturnVoid, 0));  // pc 10
        s.c_clinit.code = c;
        s.c_clinit.registers_size = 1;
    }
    // LBad.<clinit>: x = 1 / 0   n m ArithmeticException
    {
        std::vector<uint16_t> c;
        c.push_back(Op11n(kOpConst4, 0, 1));  // pc 0
        c.push_back(Op11n(kOpConst4, 1, 0));  // pc 1
        Op23x(&c, kOpDivInt, 0, 0, 1);        // pc 2
        Op21c(&c, kOpSput, 0, kFieldBadX);    // pc 4
        c.push_back(Op11x(kOpReturnVoid, 0)); // pc 6
        s.bad_clinit.code = c;
        s.bad_clinit.registers_size = 2;
    }

    // LE.catchArith(a, b): try { return a/b; } catch (ArithmeticException) { return -1; }
    {
        std::vector<uint16_t> c;
        Op23x(&c, kOpDivInt, 0, 1, 2);              // pc 0..1  (trong try)
        c.push_back(Op11x(kOpReturn, 0));           // pc 2
        c.push_back(Op11x(kOpMoveException, 0));    // pc 3  ← handler
        c.push_back(Op11n(kOpConst4, 0, -1));       // pc 4
        c.push_back(Op11x(kOpReturn, 0));           // pc 5
        s.catch_arith.code = c;
        s.catch_arith.registers_size = 3;
        s.catch_arith.ins_size = 2;
        TrySpec t;
        t.start_addr = 0;
        t.insn_count = 2;
        t.handlers = {{"Ljava/lang/ArithmeticException;", 3}};
        s.catch_arith.tries = {t};
    }

    // LE.catchTwo(a, b): handler  u (MyEx) kh ng kh p, handler th  hai kh p.
    {
        std::vector<uint16_t> c;
        Op23x(&c, kOpDivInt, 0, 1, 2);           // pc 0..1
        c.push_back(Op11x(kOpReturn, 0));        // pc 2
        c.push_back(Op11x(kOpMoveException, 0)); // pc 3  ← handler MyEx
        c.push_back(Op11n(kOpConst4, 0, 1));     // pc 4
        c.push_back(Op11x(kOpReturn, 0));        // pc 5
        c.push_back(Op11x(kOpMoveException, 0)); // pc 6  ← handler ArithmeticException
        c.push_back(Op11n(kOpConst4, 0, 2));     // pc 7
        c.push_back(Op11x(kOpReturn, 0));        // pc 8
        s.catch_two.code = c;
        s.catch_two.registers_size = 3;
        s.catch_two.ins_size = 2;
        TrySpec t;
        t.start_addr = 0;
        t.insn_count = 2;
        t.handlers = {{"LMyEx;", 3}, {"Ljava/lang/ArithmeticException;", 6}};
        s.catch_two.tries = {t};
    }

    // LE.catchAll(): try { throw new MyEx(); } catch (m i th ) { return 7; }
    {
        std::vector<uint16_t> c;
        Op21c(&c, kOpNewInstance, 0, kTypeMyEx);            // pc 0..1
        Op35c(&c, kOpInvokeDirect, kMethodMyExCtor, {0});   // pc 2..4
        c.push_back(Op11x(kOpThrow, 0));                    // pc 5
        c.push_back(Op11x(kOpMoveException, 0));            // pc 6  ← handler
        Op21s(&c, kOpConst16, 0, 7);                        // pc 7..8
        c.push_back(Op11x(kOpReturn, 0));                   // pc 9
        s.catch_all.code = c;
        s.catch_all.registers_size = 1;
        s.catch_all.outs_size = 1;
        TrySpec t;
        t.start_addr = 0;
        t.insn_count = 6;
        t.handlers = {{"", 6}};  // type r ng = catch-all
        s.catch_all.tries = {t};
    }

    // LE.catchCheck(): b t b ng RuntimeException r i ki m tra object  ng l  MyEx.
    {
        std::vector<uint16_t> c;
        Op21c(&c, kOpNewInstance, 0, kTypeMyEx);          // pc 0..1
        Op35c(&c, kOpInvokeDirect, kMethodMyExCtor, {0}); // pc 2..4
        c.push_back(Op11x(kOpThrow, 0));                  // pc 5
        c.push_back(Op11x(kOpMoveException, 0));          // pc 6  ← handler
        Op22c(&c, kOpInstanceOf, 0, 0, kTypeMyEx);        // pc 7..8
        c.push_back(Op11x(kOpReturn, 0));                 // pc 9
        s.catch_check.code = c;
        s.catch_check.registers_size = 1;
        s.catch_check.outs_size = 1;
        TrySpec t;
        t.start_addr = 0;
        t.insn_count = 6;
        t.handlers = {{"Ljava/lang/RuntimeException;", 6}};
        s.catch_check.tries = {t};
    }

    // LE.noMatch(a, b): ch  b t MyEx n n ArithmeticException ph i l t l n.
    {
        std::vector<uint16_t> c;
        Op23x(&c, kOpDivInt, 0, 1, 2);           // pc 0..1
        c.push_back(Op11x(kOpReturn, 0));        // pc 2
        c.push_back(Op11x(kOpMoveException, 0)); // pc 3
        c.push_back(Op11n(kOpConst4, 0, 5));     // pc 4
        c.push_back(Op11x(kOpReturn, 0));        // pc 5
        s.no_match.code = c;
        s.no_match.registers_size = 3;
        s.no_match.ins_size = 2;
        TrySpec t;
        t.start_addr = 0;
        t.insn_count = 2;
        t.handlers = {{"LMyEx;", 3}};
        s.no_match.tries = {t};
    }

    // LE.uncaught(): n m m  kh ng c  try.
    {
        std::vector<uint16_t> c;
        Op21c(&c, kOpNewInstance, 0, kTypeMyEx);
        Op35c(&c, kOpInvokeDirect, kMethodMyExCtor, {0});
        c.push_back(Op11x(kOpThrow, 0));
        s.uncaught.code = c;
        s.uncaught.registers_size = 1;
        s.uncaught.outs_size = 1;
    }

    // LE.thrower(): n m MyEx, kh ng b t.
    {
        std::vector<uint16_t> c;
        Op21c(&c, kOpNewInstance, 0, kTypeMyEx);
        Op35c(&c, kOpInvokeDirect, kMethodMyExCtor, {0});
        c.push_back(Op11x(kOpThrow, 0));
        s.thrower.code = c;
        s.thrower.registers_size = 1;
        s.thrower.outs_size = 1;
    }

    // LE.callerCatches(): try { thrower(); return 1; } catch (m i th ) { return 9; }
    {
        std::vector<uint16_t> c;
        Op35c(&c, kOpInvokeStatic, kMethodThrower, {});  // pc 0..2
        c.push_back(Op11n(kOpConst4, 0, 1));             // pc 3
        c.push_back(Op11x(kOpReturn, 0));                // pc 4
        c.push_back(Op11x(kOpMoveException, 0));         // pc 5  ← handler
        Op21s(&c, kOpConst16, 0, 9);                     // pc 6..7
        c.push_back(Op11x(kOpReturn, 0));                // pc 8
        s.caller_catches.code = c;
        s.caller_catches.registers_size = 1;
        TrySpec t;
        t.start_addr = 0;
        t.insn_count = 3;
        t.handlers = {{"", 5}};
        s.caller_catches.tries = {t};
    }

    const auto simple_sget = [](MethodSpec* m, uint16_t field_idx) {
        std::vector<uint16_t> c;
        Op21c(&c, kOpSget, 0, field_idx);
        c.push_back(Op11x(kOpReturn, 0));
        m->code = c;
        m->registers_size = 1;
    };
    simple_sget(&s.read_c, kFieldCInit);
    simple_sget(&s.read_counter, kFieldCounterN);
    simple_sget(&s.read_bad, kFieldBadX);

    DexBuilder builder;
    const std::vector<uint8_t> dex = builder.Build(BuildClasses(s));
    std::printf("DEX synthetic: %zu bytes\n", dex.size());
Check(builder.TypeIndexOf("LMyEx;") == kTypeMyEx, "index  n  nh gi a hai l t build");

    kudroid::kuart::DexClassLinker linker;
    std::string error;
    if (!linker.AddDexFile(dex.data(), dex.size(), "test.dex", &error)) {
        std::printf("  FAIL AddDexFile: %s\n=== FAILED ===\n", error.c_str());
        return 1;
    }

    kudroid::kuart::DexClass* e = linker.FindClass("LE;");
    if (e == nullptr) {
        std::printf("  FAIL FindClass(LE;): %s\n=== FAILED ===\n", linker.last_error().c_str());
        return 1;
    }

    kudroid::kuart::Interpreter interp(&linker);

    struct CallResult {
        DexValue value;
        bool threw = false;
        std::string trace;
    };
    const auto call = [&](const char* name, const char* sig,
                          std::vector<DexValue> args) -> CallResult {
        CallResult r;
        interp.ClearPendingException();
        kudroid::kuart::DexMethod* m = e->FindDirectMethod(name, sig);
        if (m == nullptr) {
std::printf("  FAIL not found %s%s\n", name, sig);
            ++g_failures;
            return r;
        }
        r.value = interp.Execute(m, args.data(), args.size());
        r.threw = interp.HasPendingException();
        r.trace = interp.pending_exception_trace();
        interp.ClearPendingException();
        return r;
    };

    // ── try/catch ──
    {
        const CallResult ok = call("catchArith", "(II)I",
                                   {DexValue::Int(84), DexValue::Int(2)});
Check(!ok.threw && ok.value.i == 42, "try kh ng n m th  ch y b nh th ng (84/2=42)");

        const CallResult caught = call("catchArith", "(II)I",
                                       {DexValue::Int(84), DexValue::Int(0)});
Check(!caught.threw, "chia 0 b  catch, kh ng l t l n caller");
Check(caught.value.i == -1, "handler ch y v  tr  -1");
    }
    {
        const CallResult r = call("catchTwo", "(II)I",
                                  {DexValue::Int(1), DexValue::Int(0)});
Check(!r.threw && r.value.i == 2, "ch n handler kh p ki u, b  qua handler kh ng kh p");
    }
    {
        const CallResult r = call("catchAll", "()I", {});
Check(!r.threw && r.value.i == 7, "catch-all b t  c throw t ng minh");
    }
    {
        const CallResult r = call("catchCheck", "()I", {});
Check(!r.threw, "b t b ng superclass (RuntimeException b t MyEx)");
Check(r.value.i == 1, "move-exception tr   ng object   n m");
    }
    {
        const CallResult r = call("noMatch", "(II)I",
                                  {DexValue::Int(1), DexValue::Int(0)});
Check(r.threw, "handler kh ng kh p ki u   exception truy n l n caller");
    }
    {
        const CallResult r = call("uncaught", "()I", {});
Check(r.threw, "method kh ng c  try th  exception truy n l n");
    }
    {
        const CallResult r = call("callerCatches", "()I", {});
Check(!r.threw, "exception c a callee b  caller b t");
Check(r.value.i == 9, "caller ch y handler c a m nh");
    }

    // ── <clinit> ──
    {
        kudroid::kuart::DexClass* c = linker.FindClass("LC;");
        Check(c != nullptr && c->status != kudroid::kuart::DexClass::Status::kInitialized,
"class ch a  c d ng th  ch a initialize");

        const CallResult first = call("readC", "()I", {});
Check(!first.threw && first.value.i == 42, "sget k ch ho t <clinit>,  c  c 42");
        Check(c->status == kudroid::kuart::DexClass::Status::kInitialized,
"class chuy n sang kInitialized");

        const CallResult second = call("readC", "()I", {});
Check(!second.threw && second.value.i == 42, "l n  c th  hai v n  ng");

        const CallResult count = call("readCounter", "()I", {});
Check(!count.threw && count.value.i == 1, "<clinit> ch  ch y M T l n");
    }
    {
        const CallResult r = call("readBad", "()I", {});
Check(r.threw, "<clinit> n m exception th  lan ra ch  d ng class");
        kudroid::kuart::DexClass* bad = linker.FindClass("LBad;");
        Check(bad != nullptr && bad->status == kudroid::kuart::DexClass::Status::kError,
"class c  <clinit> error b   nh d u kError");
    }

    // ── stack trace ──
    //
    // An exception used to be reported as a bare message with no location, so a
    // failure deep in guest bytecode could not be traced to the method that caused
    // it. The frames are captured at throw time because the C++ frames are already
    // gone by the time a caller reports the exception.
    {
        const CallResult r = call("uncaught", "()I", {});
        Check(r.threw, "uncaught still propagates");
        Check(r.trace.find("E.uncaught") != std::string::npos,
              "stack trace names the throwing method");
        std::printf("  trace:\n%s", r.trace.c_str());
    }
    {
        // Nested: the callee throws, the caller has no handler, so both frames must
        // appear with the innermost one first.
        const CallResult r = call("noMatch", "(II)I",
                                  {DexValue::Int(1), DexValue::Int(0)});
        Check(r.threw, "noMatch propagates");
        const size_t inner = r.trace.find("E.noMatch");
        Check(inner != std::string::npos, "nested trace contains the throwing frame");
    }
    {
        // Caught exceptions leave nothing behind: a stale trace would later be
        // attributed to an unrelated failure.
        const CallResult r = call("catchArith", "(II)I",
                                  {DexValue::Int(1), DexValue::Int(0)});
        Check(!r.threw, "caught exception does not propagate");
        Check(interp.pending_exception_trace().empty(),
              "trace cleared once the exception is handled");
    }

std::printf("=== %s (%d error) ===\n", g_failures == 0 ? "PASSED" : "FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}
