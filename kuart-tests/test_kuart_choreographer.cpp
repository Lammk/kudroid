// test_kuart_choreographer.cpp — the Java frame-callback API, driven through the real
// framework.dex. Unity needs getInstance()/postFrameCallback to work, and auto-stubs
// with null signatures must not silently resolve to the wrong overload.
#include "kudroid/framework_dex_bytes.h"
#include "kudroid/kuart/DexClassLinker.h"
#include "kudroid/kuart/DexJniEnv.h"
#include "kudroid/kuart/DexObject.h"
#include "kudroid/kuart/DexString.h"
#include "kudroid/kuart/Interpreter.h"
#include "kudroid/platform/FramePacer.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
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

using dexbuild::ClassSpec;
using dexbuild::DexBuilder;
using dexbuild::FieldSpec;
using dexbuild::MethodSpec;
using kudroid::kuart::DexClass;
using kudroid::kuart::DexClassLinker;
using kudroid::kuart::DexField;
using kudroid::kuart::DexJniEnv;
using kudroid::kuart::DexMethod;
using kudroid::kuart::DexObject;
using kudroid::kuart::DexValue;
using kudroid::kuart::Interpreter;

DexClassLinker* g_linker = nullptr;
Interpreter* g_interp = nullptr;

// ── minimal DEX encoding helpers, as the other kuart tests use ──────────────

uint16_t Op11x(uint8_t op, uint8_t a) { return static_cast<uint16_t>(op | (a << 8)); }
void Op21c(std::vector<uint16_t>* code, uint8_t op, uint8_t a, uint16_t idx) {
    code->push_back(static_cast<uint16_t>(op | (a << 8)));
    code->push_back(idx);
}

constexpr uint8_t kOpReturnVoid = 0x0e;
constexpr uint8_t kOpReturn = 0x0f;
constexpr uint8_t kOpConst4 = 0x12;
constexpr uint8_t kOpMoveResult = 0x0a;
constexpr uint8_t kOpSputWide = 0x6a;
constexpr uint8_t kOpInvokeVirtual = 0x6e;
constexpr uint32_t kAccPublic = 0x1;
constexpr uint32_t kAccStatic = 0x8;
constexpr uint32_t kAccConstructor = 0x10000;

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

// Call a static method; a pending exception is a failure with its message.
bool CallStatic(const char* descriptor, const char* name, const char* sig,
                std::vector<DexValue> args, DexValue* out, const char* what) {
    DexClass* klass = g_linker->FindClass(descriptor);
    if (klass == nullptr || klass->is_stub) {
        std::printf("  FAIL %s: class %s missing or a stub\n", what, descriptor);
        ++g_failures;
        ++g_checks;
        return false;
    }
    g_interp->ClearPendingException();
    if (!g_interp->EnsureInitialized(klass)) {
        std::printf("  FAIL %s: <clinit> failed: %s\n", what, g_interp->last_error().c_str());
        ++g_failures;
        ++g_checks;
        g_interp->ClearPendingException();
        return false;
    }
    DexMethod* m = klass->FindDirectMethod(name, sig);
    if (m == nullptr) m = klass->FindVirtualMethod(name, sig);
    if (m == nullptr) {
        std::printf("  FAIL %s: no method %s%s\n", what, name, sig);
        ++g_failures;
        ++g_checks;
        return false;
    }
    const DexValue r = g_interp->Execute(m, args.data(), args.size());
    if (g_interp->HasPendingException()) {
        std::printf("  FAIL %s threw: %s\n", what, g_interp->last_error().c_str());
        ++g_failures;
        ++g_checks;
        g_interp->ClearPendingException();
        return false;
    }
    if (out != nullptr) *out = r;
    return true;
}

bool CallVirtual(DexObject* receiver, const char* name, const char* sig,
                 std::vector<DexValue> args, DexValue* out, const char* what) {
    if (receiver == nullptr || receiver->clazz == nullptr) {
        std::printf("  FAIL %s: null receiver\n", what);
        ++g_failures;
        ++g_checks;
        return false;
    }
    DexMethod* m = receiver->clazz->FindVirtualMethod(name, sig);
    if (m == nullptr) m = receiver->clazz->FindDirectMethod(name, sig);
    if (m == nullptr) {
        std::printf("  FAIL %s: no method %s%s on %s\n", what, name, sig,
                    receiver->clazz->PrettyName().c_str());
        ++g_failures;
        ++g_checks;
        return false;
    }
    std::vector<DexValue> full;
    full.push_back(DexValue::Ref(receiver));
    for (const DexValue& v : args) full.push_back(v);

    g_interp->ClearPendingException();
    const DexValue r = g_interp->Execute(m, full.data(), full.size());
    if (g_interp->HasPendingException()) {
        std::printf("  FAIL %s threw: %s\n", what, g_interp->last_error().c_str());
        ++g_failures;
        ++g_checks;
        g_interp->ClearPendingException();
        return false;
    }
    if (out != nullptr) *out = r;
    return true;
}

template <typename Fn>
bool wait_until(Fn&& done, int timeout_ms = 3000) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return done();
}

// ── the FrameCallback the tests post ─────────────────────────────────────────
//
// A real Java class implementing android.view.Choreographer$FrameCallback, built with
// dex_builder: doFrame(J) writes its argument into a static long. That is the whole
// recorder — if the field changes, the pacer delivered the frame AND the interpreter
// dispatched doFrame on the object, which is the pair of facts under test.
//
// A synthesised class rather than a Proxy, deliberately: a Proxy would also exercise
// InvocationHandler dispatch, and a failure there would be indistinguishable from a
// failure in frame delivery.

struct CallbackSpecs {
    FieldSpec last_frame_time{"lastFrameTime", "J", kAccPublic | kAccStatic};
    MethodSpec ctor;
    MethodSpec do_frame;

    // Bytecode that calls String.length(I)I — an overload that does not exist, so
    // ResolveMethod auto-stubs it. This is what makes the signature defect observable:
    // with a null signature the stub matches the real length() by name alone.
    MethodSpec probe_bad_overload;
    dexbuild::MethodRefSpec string_length_int{"Ljava/lang/String;", "length", "I", {"I"}};

    CallbackSpecs() {
        ctor.name = "<init>";
        ctor.access_flags = kAccPublic | kAccConstructor;

        do_frame.name = "doFrame";
        do_frame.return_type = "V";
        do_frame.params = {"J"};
        do_frame.access_flags = kAccPublic;

        probe_bad_overload.name = "probeBadOverload";
        probe_bad_overload.return_type = "I";
        probe_bad_overload.params = {"Ljava/lang/String;"};
        probe_bad_overload.access_flags = kAccPublic | kAccStatic;
    }
};

std::vector<ClassSpec> BuildCallbackClasses(const CallbackSpecs& s) {
    ClassSpec cb;
    cb.descriptor = "Lcom/foo/Cb;";
    cb.interfaces = {"Landroid/view/Choreographer$FrameCallback;"};
    cb.static_fields = {s.last_frame_time};
    cb.direct_methods = {s.ctor, s.probe_bad_overload};
    cb.virtual_methods = {s.do_frame};
    cb.extra_method_refs = {s.string_length_int};
    return {cb};
}

// ─────────────────────────────────────────────────────────────────────────────

void test_class_is_real_not_a_stub() {
    std::printf("[choreographer] the class and its interface are real, not auto-stubs\n");

    DexClass* ch = g_linker->FindClass("Landroid/view/Choreographer;");
    Check(ch != nullptr && !ch->is_stub, "android.view.Choreographer is present");

    DexClass* cb = g_linker->FindClass("Landroid/view/Choreographer$FrameCallback;");
    Check(cb != nullptr && !cb->is_stub, "FrameCallback is present");

    // The interface must DECLARE doFrame. It previously declared nothing at all, which
    // is worse than the class being absent: postFrameCallback could be handed an
    // implementation and there would be nothing on it to call.
    if (cb != nullptr) {
        DexMethod* do_frame = cb->FindVirtualMethod("doFrame", "(J)V");
        Check(do_frame != nullptr,
              "FrameCallback declares doFrame(J)V — the interface used to be empty, so a "
              "posted callback had no method to invoke");
    }
}

// THE test. One line, and it is the whole failure.
void test_get_instance_is_not_null() {
    std::printf("[choreographer] getInstance() returns an instance, never null\n");

    DexValue out;
    if (!CallStatic("Landroid/view/Choreographer;", "getInstance",
                    "()Landroid/view/Choreographer;", {}, &out, "getInstance")) {
        return;
    }
    Check(out.l != nullptr,
          "getInstance() is non-null — null is what the auto-stub returned, and Unity's "
          "JNIBridge read it as 'no frame source' and threw NoSuchMethodError out of "
          "Looper.loop");

    // Per-thread identity: called twice on one thread it must be the same object, since
    // an app caches it and posts through the cached reference.
    DexValue again;
    if (CallStatic("Landroid/view/Choreographer;", "getInstance",
                   "()Landroid/view/Choreographer;", {}, &again, "getInstance again")) {
        Check(again.l == out.l, "and the same thread gets the same instance");
    }
}

void test_methods_have_bodies() {
    std::printf("[choreographer] every method ULTRAKILL calls has a real body\n");

    DexClass* ch = g_linker->FindClass("Landroid/view/Choreographer;");
    if (ch == nullptr) {
        Check(false, "class present");
        return;
    }

    struct Wanted {
        const char* name;
        const char* sig;
        bool is_static;
    };
    // The exact three kuart_verify reported against ULTRAKILL's classes.dex, plus the
    // remove/query pair a frame pacer uses.
    const Wanted wanted[] = {
        {"getInstance", "()Landroid/view/Choreographer;", true},
        {"postFrameCallback", "(Landroid/view/Choreographer$FrameCallback;)V", false},
        {"postFrameCallbackDelayed", "(Landroid/view/Choreographer$FrameCallback;J)V", false},
        {"removeFrameCallback", "(Landroid/view/Choreographer$FrameCallback;)V", false},
        {"getFrameIntervalNanos", "()J", false},
        {"getFrameTimeNanos", "()J", false},
    };
    for (const Wanted& w : wanted) {
        DexMethod* m = w.is_static ? ch->FindDirectMethod(w.name, w.sig)
                                   : ch->FindVirtualMethod(w.name, w.sig);
        // A body OR a native binding. An auto-stub has neither, and that is exactly what
        // it was: code_item null, not native, so Execute fell through to returning
        // nothing at all.
        const bool usable = m != nullptr && (m->code_item != nullptr || m->IsNative());
        Check(usable, std::string(w.name) + w.sig + " is callable");
    }
}

// The interval must come from the pacer, so the Java and native halves cannot disagree.
// Two frame sources in one process means two clocks, and a guest that posts on both
// paces against whichever it read last.
void test_frame_interval_matches_the_native_pacer() {
    std::printf("[choreographer] the Java interval is the pacer's, not a second constant\n");

    DexValue instance;
    if (!CallStatic("Landroid/view/Choreographer;", "getInstance",
                    "()Landroid/view/Choreographer;", {}, &instance, "getInstance")) {
        return;
    }
    DexValue interval;
    if (!CallVirtual(instance.l, "getFrameIntervalNanos", "()J", {}, &interval,
                     "getFrameIntervalNanos")) {
        return;
    }
    Check(interval.j == kudroid::frame_pacer_interval_ns(),
          std::string("Java reports ") + std::to_string(interval.j) +
              " ns, the same value the NDK pacer reports");
    Check(interval.j >= 4000000 && interval.j <= 41666667,
          "and it is a plausible display period (240Hz..24Hz)");

    // A rate hint applied through the NDK entry point must be visible through the Java
    // getter, which is the observable proof that one source backs both.
    kudroid::frame_pacer_request_rate(30.0f);
    DexValue after;
    if (CallVirtual(instance.l, "getFrameIntervalNanos", "()J", {}, &after, "interval at 30fps")) {
        Check(after.j > interval.j,
              "a setFrameRate(30) through the NDK lengthens the interval the JAVA getter "
              "reports — one frame source, not two");
    }
    kudroid::frame_pacer_request_rate(0.0f);
}

void test_frame_time_is_monotonic_nanos() {
    std::printf("[choreographer] getFrameTimeNanos is CLOCK_MONOTONIC nanoseconds\n");

    DexValue instance;
    if (!CallStatic("Landroid/view/Choreographer;", "getInstance",
                    "()Landroid/view/Choreographer;", {}, &instance, "getInstance")) {
        return;
    }

    struct timespec ts {};
    ::clock_gettime(CLOCK_MONOTONIC, &ts);
    const int64_t before =
        static_cast<int64_t>(ts.tv_sec) * 1000000000ll + static_cast<int64_t>(ts.tv_nsec);

    DexValue t;
    if (!CallVirtual(instance.l, "getFrameTimeNanos", "()J", {}, &t, "getFrameTimeNanos")) {
        return;
    }
    ::clock_gettime(CLOCK_MONOTONIC, &ts);
    const int64_t after =
        static_cast<int64_t>(ts.tv_sec) * 1000000000ll + static_cast<int64_t>(ts.tv_nsec);

    // Outside a callback the platform's own docs say the value is undefined; KuDroid
    // answers "now" rather than throwing, because apps read it defensively from
    // arbitrary threads and an exception there turns a diagnostic read into a crash.
    Check(t.j >= before && t.j <= after,
          "outside a frame it answers now, on the same clock System.nanoTime reports "
          "— not mach_absolute_time, which does not advance across sleep");
}

// A null callback must be rejected by the Java layer, not carried into the pacer where
// it would be dropped silently. An app passing null has a bug and needs to be told.
void test_null_callback_throws() {
    std::printf("[choreographer] posting a null callback throws, rather than vanishing\n");

    DexValue instance;
    if (!CallStatic("Landroid/view/Choreographer;", "getInstance",
                    "()Landroid/view/Choreographer;", {}, &instance, "getInstance")) {
        return;
    }

    DexClass* ch = g_linker->FindClass("Landroid/view/Choreographer;");
    DexMethod* post =
        ch != nullptr ? ch->FindVirtualMethod(
                            "postFrameCallback", "(Landroid/view/Choreographer$FrameCallback;)V")
                      : nullptr;
    if (post == nullptr) {
        Check(false, "postFrameCallback exists");
        return;
    }
    const DexValue args[2] = {DexValue::Ref(instance.l), DexValue::Ref(nullptr)};
    g_interp->ClearPendingException();
    g_interp->Execute(post, args, 2);
    const bool threw = g_interp->HasPendingException();
    Check(threw, "a null callback raises rather than being queued and forgotten");
    g_interp->ClearPendingException();

    // And nothing was left queued by the attempt.
    Check(kudroid::frame_pacer_pending_count() == 0, "the pacer queue is untouched");
}

// removeFrameCallback(null) must be a no-op. Apps call it during teardown without
// checking, and throwing there would turn correct cleanup into a crash.
void test_remove_null_is_harmless() {
    std::printf("[choreographer] removeFrameCallback(null) is a no-op\n");

    DexValue instance;
    if (!CallStatic("Landroid/view/Choreographer;", "getInstance",
                    "()Landroid/view/Choreographer;", {}, &instance, "getInstance")) {
        return;
    }
    DexClass* ch = g_linker->FindClass("Landroid/view/Choreographer;");
    DexMethod* remove =
        ch != nullptr ? ch->FindVirtualMethod(
                            "removeFrameCallback",
                            "(Landroid/view/Choreographer$FrameCallback;)V")
                      : nullptr;
    if (remove == nullptr) {
        Check(false, "removeFrameCallback exists");
        return;
    }
    const DexValue args[2] = {DexValue::Ref(instance.l), DexValue::Ref(nullptr)};
    g_interp->ClearPendingException();
    g_interp->Execute(remove, args, 2);
    Check(!g_interp->HasPendingException(),
          "no exception — teardown code calls this without a null check");
    g_interp->ClearPendingException();
}

// A posted callback must actually reach Java. Everything above proves the methods
// exist; this proves the frame arrives and doFrame runs with the frame's timestamp.
//
// This is the end-to-end path Unity depends on: Java post → native pacer → back into
// the interpreter → the app's doFrame. Any break in it leaves the app waiting forever,
// which is precisely how the captured session ended.
void test_posted_callback_reaches_java(DexObject* callback, DexField* last_frame_time) {
    std::printf("[choreographer] a posted callback reaches Java and doFrame runs\n");
    if (callback == nullptr || last_frame_time == nullptr) {
        Check(false, "test callback object was built");
        return;
    }

    DexClass* cb_class = g_linker->ClassOfObject(callback);
    if (cb_class == nullptr) {
        Check(false, "callback has a usable class");
        return;
    }
    // Clear the recorder, so a stale value cannot pass for a delivery.
    cb_class->static_values[last_frame_time->offset_or_slot] = DexValue::Long(0);

    DexValue instance;
    if (!CallStatic("Landroid/view/Choreographer;", "getInstance",
                    "()Landroid/view/Choreographer;", {}, &instance, "getInstance")) {
        return;
    }

    struct timespec ts {};
    ::clock_gettime(CLOCK_MONOTONIC, &ts);
    const int64_t before =
        static_cast<int64_t>(ts.tv_sec) * 1000000000ll + static_cast<int64_t>(ts.tv_nsec);

    if (!CallVirtual(instance.l, "postFrameCallback",
                     "(Landroid/view/Choreographer$FrameCallback;)V",
                     {DexValue::Ref(callback)}, nullptr, "postFrameCallback")) {
        return;
    }
    Check(kudroid::frame_pacer_pending_count() >= 1, "the callback is queued in the pacer");

    // The pacer delivers on its own thread when nobody polls a looper, which is the
    // case here — so this waits rather than driving anything by hand.
    const bool arrived = wait_until([&] {
        return cb_class->static_values[last_frame_time->offset_or_slot].j != 0;
    });
    Check(arrived,
          "doFrame ran without the test driving the pacer — Java post, native pacer, "
          "back into the interpreter");

    const int64_t seen = cb_class->static_values[last_frame_time->offset_or_slot].j;
    ::clock_gettime(CLOCK_MONOTONIC, &ts);
    const int64_t after =
        static_cast<int64_t>(ts.tv_sec) * 1000000000ll + static_cast<int64_t>(ts.tv_nsec);
    Check(seen >= before && seen <= after,
          std::string("and the frame time it received (") + std::to_string(seen) +
              ") lies between two CLOCK_MONOTONIC readings around the post");

    // One-shot, as on Android: no further delivery without another post. Re-arming
    // automatically would make a guest render twice per frame.
    cb_class->static_values[last_frame_time->offset_or_slot] = DexValue::Long(0);
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    Check(cb_class->static_values[last_frame_time->offset_or_slot].j == 0,
          "and it does not fire again on its own — the API is one-shot");
}

// A removed callback must not fire. An app cancels its pending frame during teardown,
// and a delivery after that runs into a half-destroyed object.
void test_removed_callback_does_not_fire(DexObject* callback, DexField* last_frame_time) {
    std::printf("[choreographer] a removed callback does not fire\n");
    if (callback == nullptr || last_frame_time == nullptr) {
        Check(false, "test callback object was built");
        return;
    }
    DexClass* cb_class = g_linker->ClassOfObject(callback);
    if (cb_class == nullptr) {
        Check(false, "callback has a usable class");
        return;
    }
    cb_class->static_values[last_frame_time->offset_or_slot] = DexValue::Long(0);

    DexValue instance;
    if (!CallStatic("Landroid/view/Choreographer;", "getInstance",
                    "()Landroid/view/Choreographer;", {}, &instance, "getInstance")) {
        return;
    }

    // Delayed well past the removal, so the race is not what is being measured.
    if (!CallVirtual(instance.l, "postFrameCallbackDelayed",
                     "(Landroid/view/Choreographer$FrameCallback;J)V",
                     {DexValue::Ref(callback), DexValue::Long(400)}, nullptr,
                     "postFrameCallbackDelayed")) {
        return;
    }
    Check(kudroid::frame_pacer_pending_count() >= 1, "queued");

    if (!CallVirtual(instance.l, "removeFrameCallback",
                     "(Landroid/view/Choreographer$FrameCallback;)V",
                     {DexValue::Ref(callback)}, nullptr, "removeFrameCallback")) {
        return;
    }
    Check(kudroid::frame_pacer_pending_count() == 0, "and dequeued by remove");

    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    Check(cb_class->static_values[last_frame_time->offset_or_slot].j == 0,
          "doFrame never ran, well past the delay it was posted with");
}

// The property above is about methods that EXIST. This one drives the auto-stub path
// itself, which is where the defect actually lived — and it is the test that fails when
// the signature is left null.
//
// Bytecode calls String.length(I)I, an overload that does not exist. ResolveMethod
// auto-stubs it, because String is boot-classpath. InvokeMethod then re-resolves the
// call against the receiver's class using target->signature:
//
//   signature "(I)I"  -> FindVirtualMethod finds nothing, the stub stays, result is 0.
//   signature null    -> NameAndSigMatch treats null as "match anything", so the REAL
//                        length() is found and runs, returning 5.
//
// Five instead of zero is the whole bug in one number. On device the same substitution
// sent Unity's getSystemService(Class) into getSystemService(String), which compared a
// java.lang.Class against its service names, matched none and returned null — leaving
// "call getDefaultDisplay on null" as the only trace.
void test_autostubbed_overload_does_not_hijack_a_real_one(DexMethod* probe) {
    std::printf("[stub] an auto-stubbed overload does not run a different real overload\n");
    if (probe == nullptr) {
        Check(false, "the probe method was built");
        return;
    }

    DexValue out;
    g_interp->ClearPendingException();
    const DexValue arg =
        DexValue::Ref(reinterpret_cast<DexObject*>(g_linker->NewString("hello")));
    out = g_interp->Execute(probe, &arg, 1);
    if (g_interp->HasPendingException()) {
        std::printf("  FAIL probe threw: %s\n", g_interp->last_error().c_str());
        ++g_failures;
        ++g_checks;
        g_interp->ClearPendingException();
        return;
    }

    Check(out.i != 5,
          std::string("String.length(I)I — which does not exist — did not silently run "
                      "String.length(), got ") +
              std::to_string(out.i) +
              " (5 would mean the stub matched a different overload by name alone)");
    Check(out.i == 0,
          "the stubbed overload returns nothing, which is what a method with no body does");
}

// ── the auto-stub signature defect ──────────────────────────────────────────
//
// ResolveMethod's auto-stub used to leave DexMethod::signature null, and
// NameAndSigMatch treats null as "match anything". InvokeMethod re-resolves every
// virtual call against the receiver's class using target->signature — so a stubbed
// overload found a DIFFERENT overload of the same name and the interpreter passed
// arguments of the wrong type to it, with no diagnostic.
//
// ULTRAKILL: Activity.getSystemService(Ljava/lang/Class;) was absent, the stub matched
// getSystemService(Ljava/lang/String;), Unity's java.lang.Class went in where a String
// was expected, every name comparison failed, and the log said only "call
// getDefaultDisplay on null".

void test_getsystemservice_class_overload_exists() {
    std::printf("[stub] Activity.getSystemService(Class) exists rather than being stubbed\n");

    DexClass* activity = g_linker->FindClass("Landroid/app/Activity;");
    if (activity == nullptr) {
        Check(false, "android.app.Activity present");
        return;
    }
    DexMethod* by_class =
        activity->FindVirtualMethod("getSystemService", "(Ljava/lang/Class;)Ljava/lang/Object;");
    Check(by_class != nullptr && by_class->code_item != nullptr,
          "getSystemService(Class) has a body — Unity calls "
          "getSystemService(WindowManager.class).getDefaultDisplay()");

    DexMethod* by_name =
        activity->FindVirtualMethod("getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
    Check(by_name != nullptr, "and the String overload is still there");
    Check(by_class != by_name,
          "the two are DISTINCT methods — a null-signature stub made them the same one");
}

// The general property, independent of any single method: looking a name up with an
// explicit signature must never return a method with a different signature.
void test_signature_lookup_is_exact() {
    std::printf("[stub] a signature-qualified lookup never returns another overload\n");

    DexClass* activity = g_linker->FindClass("Landroid/app/Activity;");
    if (activity == nullptr) {
        Check(false, "android.app.Activity present");
        return;
    }
    // Ask for a signature that does not exist. The answer must be null, not "whichever
    // getSystemService happened to be first".
    DexMethod* bogus =
        activity->FindVirtualMethod("getSystemService", "(Ljava/lang/Integer;)Ljava/lang/Object;");
    Check(bogus == nullptr,
          "an overload that does not exist resolves to nothing, rather than silently to a "
          "different one");

    // And every method that IS found carries the signature that was asked for.
    const char* sigs[] = {
        "(Ljava/lang/String;)Ljava/lang/Object;",
        "(Ljava/lang/Class;)Ljava/lang/Object;",
    };
    bool all_exact = true;
    for (const char* sig : sigs) {
        DexMethod* m = activity->FindVirtualMethod("getSystemService", sig);
        if (m == nullptr || m->signature == nullptr || std::strcmp(m->signature, sig) != 0) {
            all_exact = false;
        }
    }
    Check(all_exact, "both real overloads report the signature they were looked up by");
}

// Context.getSystemService(Class) has to answer from the same table the String overload
// uses. A second Class-to-name mapping would drift, and a service reachable one way but
// not the other is indistinguishable — from the app's side — from one never implemented.
void test_getsystemservice_class_returns_the_window_manager() {
    std::printf("[stub] getSystemService(Class) resolves through the String table\n");

    // A concrete Context to call it on. ContextThemeWrapper is the nearest concrete
    // class in the chain that needs no Activity lifecycle.
    DexClass* ctx_class = g_linker->FindClass("Landroid/app/ContextThemeWrapper;");
    if (ctx_class == nullptr || ctx_class->is_stub) {
        Check(false, "ContextThemeWrapper present");
        return;
    }
    g_interp->ClearPendingException();
    if (!g_interp->EnsureInitialized(ctx_class)) {
        std::printf("  FAIL <clinit> of ContextThemeWrapper: %s\n",
                    g_interp->last_error().c_str());
        ++g_failures;
        ++g_checks;
        g_interp->ClearPendingException();
        return;
    }
    DexObject* ctx = g_linker->AllocObject(ctx_class);
    if (ctx == nullptr) {
        Check(false, "allocated a Context");
        return;
    }

    // WindowManager.class, the exact argument Unity passes.
    DexClass* wm = g_linker->FindClass("Landroid/view/WindowManager;");
    if (wm == nullptr) {
        Check(false, "android.view.WindowManager present");
        return;
    }
    DexObject* wm_class_obj = reinterpret_cast<DexObject*>(g_linker->GetClassObject(wm));

    DexValue service;
    if (!CallVirtual(ctx, "getSystemService", "(Ljava/lang/Class;)Ljava/lang/Object;",
                     {DexValue::Ref(wm_class_obj)}, &service, "getSystemService(Class)")) {
        return;
    }
    Check(service.l != nullptr,
          "getSystemService(WindowManager.class) returns a manager — null here is what "
          "produced 'call getDefaultDisplay on null'");

    // And it really is a WindowManager, so the caller's cast succeeds.
    if (service.l != nullptr) {
        DexClass* actual = g_linker->ClassOfObject(service.l);
        bool implements_wm = false;
        for (DexClass* k = actual; k != nullptr && !implements_wm; k = k->superclass) {
            if (k == wm) implements_wm = true;
            for (DexClass* iface : k->interfaces) {
                if (iface == wm) { implements_wm = true; break; }
            }
        }
        Check(implements_wm,
              std::string("and it is a WindowManager (") +
                  (actual != nullptr ? actual->PrettyName() : "?") +
                  ") — isInstance is checked in Java, so a wrong guess would have "
                  "returned null rather than a bad cast");
    }

    // A class with no manager must be null rather than the first service in the list.
    DexClass* unrelated = g_linker->FindClass("Ljava/lang/StringBuilder;");
    if (unrelated != nullptr) {
        DexObject* unrelated_obj =
            reinterpret_cast<DexObject*>(g_linker->GetClassObject(unrelated));
        DexValue none;
        if (CallVirtual(ctx, "getSystemService", "(Ljava/lang/Class;)Ljava/lang/Object;",
                        {DexValue::Ref(unrelated_obj)}, &none, "getSystemService(bogus)")) {
            Check(none.l == nullptr,
                  "a class with no manager returns null, not whichever service came first");
        }
    }
}

}  // namespace

int main() {
    std::printf("=== android.view.Choreographer + auto-stub signatures ===\n");

    // The synthetic callback class: doFrame(J) writes its argument into a static long.
    CallbackSpecs probe;
    DexBuilder index_builder;
    index_builder.Build(BuildCallbackClasses(probe));
    const uint16_t kFieldLastFrameTime = static_cast<uint16_t>(
        index_builder.FieldIndexOf("Lcom/foo/Cb;", probe.last_frame_time));
    const uint16_t kStringLengthInt =
        static_cast<uint16_t>(index_builder.MethodRefIndexOf(probe.string_length_int));

    CallbackSpecs cbs;
    cbs.ctor.code = {Op11x(kOpReturnVoid, 0)};
    cbs.ctor.registers_size = 1;
    cbs.ctor.ins_size = 1;
    {
        // doFrame(this=v1, frameTimeNanos=v2:v3): Cb.lastFrameTime = frameTimeNanos.
        // sput-wide takes the low register of the pair.
        std::vector<uint16_t> c;
        Op21c(&c, kOpSputWide, 2, kFieldLastFrameTime);
        c.push_back(Op11x(kOpReturnVoid, 0));
        cbs.do_frame.code = c;
        cbs.do_frame.registers_size = 4;  // this + a wide parameter
        cbs.do_frame.ins_size = 3;
        cbs.do_frame.outs_size = 0;
    }
    {
        // probeBadOverload(String s): return s.length(0);
        //
        // String.length(I)I does not exist. With the stub carrying its real signature
        // the call resolves to nothing and returns 0; with a null signature it matches
        // the real length() and returns 5 for "hello".
        // Registers: v0 = const 0 / result, p0 = s (v1).
        std::vector<uint16_t> c;
        c.push_back(Op11x(kOpConst4, 0));  // const/4 v0, 0
        Op35c(&c, kOpInvokeVirtual, kStringLengthInt, {1, 0});
        c.push_back(Op11x(kOpMoveResult, 0));
        c.push_back(Op11x(kOpReturn, 0));
        cbs.probe_bad_overload.code = c;
        cbs.probe_bad_overload.registers_size = 2;
        cbs.probe_bad_overload.ins_size = 1;
        cbs.probe_bad_overload.outs_size = 2;
    }

    DexBuilder builder;
    const std::vector<uint8_t> test_dex = builder.Build(BuildCallbackClasses(cbs));

    DexClassLinker linker;
    std::string error;
    if (!linker.AddDexFile(g_framework_dex_bytes, g_framework_dex_size, "framework.dex",
                           &error)) {
        std::printf("  FAIL AddDexFile(framework.dex): %s\n=== FAILED ===\n", error.c_str());
        return 1;
    }
    if (!linker.AddDexFile(test_dex.data(), test_dex.size(), "test.dex", &error)) {
        std::printf("  FAIL AddDexFile(test.dex): %s\n=== FAILED ===\n", error.c_str());
        return 1;
    }
    std::printf("framework.dex: %zu bytes\n", g_framework_dex_size);

    Interpreter interp(&linker);
    DexJniEnv jni(&linker, &interp);
    interp.set_jni_env(&jni);
    g_linker = &linker;
    g_interp = &interp;
    interp.set_instruction_limit(2000ull * 1000ull * 1000ull);

    kudroid::frame_pacer_reset_for_test();

    // Instantiate the callback once, for the two delivery tests.
    DexObject* callback = nullptr;
    DexField* last_frame_time = nullptr;
    DexMethod* probe_bad_overload = nullptr;
    {
        DexClass* cb_class = linker.FindClass("Lcom/foo/Cb;");
        if (cb_class != nullptr && interp.EnsureInitialized(cb_class)) {
            callback = linker.AllocObject(cb_class);
            last_frame_time = cb_class->FindStaticField("lastFrameTime", "J");
            probe_bad_overload =
                cb_class->FindDirectMethod("probeBadOverload", "(Ljava/lang/String;)I");
        }
    }

    test_class_is_real_not_a_stub();
    test_get_instance_is_not_null();
    test_methods_have_bodies();
    test_frame_interval_matches_the_native_pacer();
    test_frame_time_is_monotonic_nanos();
    test_posted_callback_reaches_java(callback, last_frame_time);
    test_removed_callback_does_not_fire(callback, last_frame_time);
    test_null_callback_throws();
    test_remove_null_is_harmless();
    test_getsystemservice_class_overload_exists();
    test_signature_lookup_is_exact();
    test_autostubbed_overload_does_not_hijack_a_real_one(probe_bad_overload);
    test_getsystemservice_class_returns_the_window_manager();

    kudroid::frame_pacer_reset_for_test();

    std::printf("=== %d checks, %d failures ===\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
