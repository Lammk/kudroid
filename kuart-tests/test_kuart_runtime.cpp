// Relaunch lifecycle regressions for the framework Looper and native wait slot.
#include "kudroid/framework_dex_bytes.h"
#include "kudroid/kuart/DexClassLinker.h"
#include "kudroid/kuart/DexJniEnv.h"
#include "kudroid/kuart/DexObject.h"
#include "kudroid/kuart/Interpreter.h"
#include "BridgeShared.h"
#include "dex_builder.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

extern "C" int kudroid_looper_is_main(int64_t ptr);

namespace {

int g_failures = 0;
int g_checks = 0;
kudroid::kuart::DexClassLinker* g_linker = nullptr;
kudroid::kuart::Interpreter* g_interp = nullptr;

using kudroid::kuart::DexClass;
using kudroid::kuart::DexClassLinker;
using kudroid::kuart::DexField;
using kudroid::kuart::DexJniEnv;
using kudroid::kuart::DexMethod;
using kudroid::kuart::DexObject;
using kudroid::kuart::DexValue;
using kudroid::kuart::Interpreter;
using dexbuild::ClassSpec;
using dexbuild::DexBuilder;
using dexbuild::MethodRefSpec;
using dexbuild::MethodSpec;

constexpr uint8_t kOp11xReturnVoid = 0x0e;
constexpr uint8_t kOp11xMoveResultObject = 0x0c;
constexpr uint8_t kOp35cInvokeVirtual = 0x6e;
constexpr uint8_t kOp35cInvokeStatic = 0x71;
constexpr uint8_t kOp11nConst4 = 0x12;
constexpr uint8_t kOp21cSputBoolean = 0x67;
constexpr uint8_t kOp21cSget = 0x60;
constexpr uint8_t kOp12xAddInt2Addr = 0xb0;
constexpr uint8_t kOp21sConst16 = 0x13;
constexpr uint8_t kOp12xMulInt2Addr = 0xb2;

uint16_t Op11x(uint8_t op, uint8_t a) {
    return static_cast<uint16_t>(op | (a << 8));
}

void Op35c(std::vector<uint16_t>* code, uint8_t op, uint16_t method_index,
           const std::vector<uint8_t>& registers) {
    code->push_back(static_cast<uint16_t>(op | (registers.size() << 12)));
    code->push_back(method_index);
    uint16_t packed = 0;
    for (size_t i = 0; i < registers.size() && i < 4; ++i) {
        packed |= static_cast<uint16_t>((registers[i] & 0xF) << (i * 4));
    }
    code->push_back(packed);
}

uint16_t Op21c(uint8_t op, uint8_t a) {
    return static_cast<uint16_t>(op | (a << 8));
}

uint16_t Op12x(uint8_t op, uint8_t a, uint8_t b) {
    return static_cast<uint16_t>(op | (a << 8) | (b << 12));
}

void Check(bool ok, const char* message) {
    ++g_checks;
    std::printf("%s %s\n", ok ? "  OK  " : "  FAIL", message);
    if (!ok) ++g_failures;
}

bool CallStatic(const char* descriptor, const char* name, const char* signature,
                DexValue* result = nullptr) {
    DexClass* klass = g_linker->FindClass(descriptor);
    if (klass == nullptr || klass->is_stub || !g_interp->EnsureInitialized(klass)) {
        std::printf("  FAIL cannot initialize %s for %s\n", descriptor, name);
        ++g_checks;
        ++g_failures;
        g_interp->ClearPendingException();
        return false;
    }
    DexMethod* method = klass->FindDirectMethod(name, signature);
    if (method == nullptr) method = klass->FindVirtualMethod(name, signature);
    if (method == nullptr) {
        std::printf("  FAIL missing %s.%s%s\n", descriptor, name, signature);
        ++g_checks;
        ++g_failures;
        return false;
    }
    g_interp->ClearPendingException();
    DexValue value = g_interp->Execute(method, nullptr, 0);
    if (g_interp->HasPendingException()) {
        std::printf("  FAIL %s threw: %s\n", name, g_interp->last_error().c_str());
        ++g_checks;
        ++g_failures;
        g_interp->ClearPendingException();
        return false;
    }
    if (result != nullptr) *result = value;
    return true;
}

bool CallVirtual(DexObject* receiver, const char* name, const char* signature,
                 const std::vector<DexValue>& args = {}, DexValue* result = nullptr) {
    if (receiver == nullptr || receiver->clazz == nullptr) {
        Check(false, "virtual call receiver is valid");
        return false;
    }
    DexMethod* method = receiver->clazz->FindVirtualMethod(name, signature);
    if (method == nullptr) method = receiver->clazz->FindDirectMethod(name, signature);
    if (method == nullptr) {
        std::printf("  FAIL missing %s%s\n", name, signature);
        ++g_checks;
        ++g_failures;
        return false;
    }
    std::vector<DexValue> full_args;
    full_args.push_back(DexValue::Ref(receiver));
    full_args.insert(full_args.end(), args.begin(), args.end());
    g_interp->ClearPendingException();
    const DexValue value = g_interp->Execute(method, full_args.data(), full_args.size());
    if (g_interp->HasPendingException()) {
        std::printf("  FAIL %s threw: %s\n", name, g_interp->last_error().c_str());
        ++g_checks;
        ++g_failures;
        g_interp->ClearPendingException();
        return false;
    }
    if (result != nullptr) *result = value;
    return true;
}

std::vector<uint8_t> BuildTeardownRunnableDex() {
    const MethodRefSpec my_looper{
        "Landroid/os/Looper;", "myLooper", "Landroid/os/Looper;", {}};
    const MethodRefSpec quit{
        "Landroid/os/Looper;", "quitForTeardown", "V", {}};

    ClassSpec probe;
    probe.descriptor = "Lcom/kudroid/TeardownRunnable;";
    probe.interfaces = {"Ljava/lang/Runnable;"};
    probe.extra_method_refs = {my_looper, quit};

    ClassSpec recorder;
    recorder.descriptor = "Lcom/kudroid/RecordingRunnable;";
    recorder.interfaces = {"Ljava/lang/Runnable;"};
    dexbuild::FieldSpec ran_field;
    ran_field.name = "sRan";
    ran_field.type = "Z";
    ran_field.access_flags = 0x9;  // public static
    recorder.static_fields = {ran_field};

    DexBuilder indexes;
    indexes.Build({probe, recorder});
    const uint16_t my_looper_index =
        static_cast<uint16_t>(indexes.MethodRefIndexOf(my_looper));
    const uint16_t quit_index = static_cast<uint16_t>(indexes.MethodRefIndexOf(quit));
    const uint16_t ran_index = static_cast<uint16_t>(
        indexes.FieldIndexOf(recorder.descriptor, ran_field));

    MethodSpec ctor;
    ctor.name = "<init>";
    ctor.access_flags = 0x1 | 0x10000;  // public constructor
    ctor.code = {Op11x(kOp11xReturnVoid, 0)};
    ctor.registers_size = 1;
    ctor.ins_size = 1;

    MethodSpec run;
    run.name = "run";
    run.return_type = "V";
    run.access_flags = 0x1;
    Op35c(&run.code, kOp35cInvokeStatic, my_looper_index, {});
    run.code.push_back(Op11x(kOp11xMoveResultObject, 0));
    Op35c(&run.code, kOp35cInvokeVirtual, quit_index, {0});
    run.code.push_back(Op11x(kOp11xReturnVoid, 0));
    run.registers_size = 1;
    run.ins_size = 1;
    run.outs_size = 1;
    probe.direct_methods = {ctor};
    probe.virtual_methods = {run};

    MethodSpec recorder_ctor;
    recorder_ctor.name = "<init>";
    recorder_ctor.access_flags = 0x1 | 0x10000;
    recorder_ctor.code = {Op11x(kOp11xReturnVoid, 0)};
    recorder_ctor.registers_size = 1;
    recorder_ctor.ins_size = 1;

    MethodSpec record_run;
    record_run.name = "run";
    record_run.return_type = "V";
    record_run.access_flags = 0x1;
    record_run.code = {
        static_cast<uint16_t>(kOp11nConst4 | (0 << 8) | (1 << 12)),
        Op21c(kOp21cSputBoolean, 0),
        ran_index,
        Op11x(kOp11xReturnVoid, 0),
    };
    record_run.registers_size = 1;
    record_run.ins_size = 1;
    recorder.direct_methods = {recorder_ctor};
    recorder.virtual_methods = {record_run};

    DexBuilder builder;
    return builder.Build({probe, recorder});
}

std::vector<uint8_t> BuildSurfaceCallbackDex() {
    ClassSpec callback;
    callback.descriptor = "Lcom/kudroid/SurfaceCallbackProbe;";
    callback.interfaces = {"Landroid/view/SurfaceHolder$Callback2;"};
    const std::vector<std::pair<std::string, std::string>> fields = {
        {"created", "I"}, {"changed", "I"}, {"redraw", "I"},
        {"destroyed", "I"}, {"width", "I"}, {"height", "I"},
        {"sequence", "I"},
    };
    for (const auto& [name, type] : fields) {
        dexbuild::FieldSpec field;
        field.name = name;
        field.type = type;
        field.access_flags = 0x9;  // public static
        callback.static_fields.push_back(field);
    }

    DexBuilder indexes;
    indexes.Build({callback});
    auto increment_method = [&](const char* method_name, const char* signature,
                                 const char* field_name, uint16_t registers) {
        const dexbuild::FieldSpec field{field_name, "I", 0x9};
        MethodSpec method;
        method.name = method_name;
        method.return_type = "V";
        method.access_flags = 0x1;
        method.registers_size = registers;
        method.ins_size = registers;
        method.code = {
            Op21c(kOp21cSget, 0),
            static_cast<uint16_t>(indexes.FieldIndexOf(callback.descriptor, field)),
            static_cast<uint16_t>(kOp11nConst4 | (1 << 8) | (1 << 12)),
            Op12x(kOp12xAddInt2Addr, 0, 1),
            Op21c(kOp21cSputBoolean, 0),
            static_cast<uint16_t>(indexes.FieldIndexOf(callback.descriptor, field)),
            Op11x(kOp11xReturnVoid, 0),
        };
        (void)signature;
        return method;
    };
    auto append_sequence = [&](MethodSpec* method, int digit) {
        const dexbuild::FieldSpec sequence_field{"sequence", "I", 0x9};
        const uint16_t index = static_cast<uint16_t>(
                indexes.FieldIndexOf(callback.descriptor, sequence_field));
        method->code.pop_back();
        method->code.insert(method->code.end(), {
            Op21c(kOp21cSget, 0), index,
            static_cast<uint16_t>(kOp21sConst16 | (1 << 8)), 10,
            Op12x(kOp12xMulInt2Addr, 0, 1),
            static_cast<uint16_t>(kOp11nConst4 | (1 << 8) | (digit << 12)),
            Op12x(kOp12xAddInt2Addr, 0, 1),
            Op21c(kOp21cSputBoolean, 0), index,
            Op11x(kOp11xReturnVoid, 0),
        });
    };

    MethodSpec created = increment_method(
            "surfaceCreated", "(Landroid/view/SurfaceHolder;)V", "created", 2);
    created.params = {"Landroid/view/SurfaceHolder;"};
    MethodSpec redraw = increment_method(
            "surfaceRedrawNeeded", "(Landroid/view/SurfaceHolder;)V", "redraw", 2);
    redraw.params = {"Landroid/view/SurfaceHolder;"};
    MethodSpec destroyed = increment_method(
            "surfaceDestroyed", "(Landroid/view/SurfaceHolder;)V", "destroyed", 2);
    destroyed.params = {"Landroid/view/SurfaceHolder;"};

    MethodSpec changed = increment_method(
            "surfaceChanged", "(Landroid/view/SurfaceHolder;III)V", "changed", 5);
    changed.params = {"Landroid/view/SurfaceHolder;", "I", "I", "I"};
    const dexbuild::FieldSpec width_field{"width", "I", 0x9};
    const dexbuild::FieldSpec height_field{"height", "I", 0x9};
    changed.code.insert(changed.code.end() - 1,
            {Op21c(kOp21cSputBoolean, 3),
             static_cast<uint16_t>(indexes.FieldIndexOf(callback.descriptor, width_field)),
             Op21c(kOp21cSputBoolean, 4),
             static_cast<uint16_t>(indexes.FieldIndexOf(callback.descriptor, height_field))});
    append_sequence(&created, 1);
    append_sequence(&changed, 2);
    append_sequence(&redraw, 3);
    append_sequence(&destroyed, 4);
    callback.virtual_methods = {created, changed, redraw, destroyed};

    DexBuilder builder;
    return builder.Build({callback});
}

bool QueuePtr(DexObject* looper, int64_t* ptr) {
    DexField* queue_field = looper->clazz->FindInstanceField(
        "mQueue", "Landroid/os/MessageQueue;");
    if (queue_field == nullptr) return false;
    DexObject* queue = looper->GetField<DexObject*>(queue_field->offset_or_slot);
    if (queue == nullptr || queue->clazz == nullptr) return false;
    DexField* ptr_field = queue->clazz->FindInstanceField("mPtr", "J");
    if (ptr_field == nullptr) return false;
    *ptr = queue->GetField<int64_t>(ptr_field->offset_or_slot);
    return true;
}

DexObject* QueueObject(DexObject* looper) {
    if (looper == nullptr || looper->clazz == nullptr) return nullptr;
    DexField* queue_field = looper->clazz->FindInstanceField(
        "mQueue", "Landroid/os/MessageQueue;");
    return queue_field != nullptr
               ? looper->GetField<DexObject*>(queue_field->offset_or_slot)
               : nullptr;
}

bool QueueHasMessages(DexObject* looper) {
    DexObject* queue = QueueObject(looper);
    if (queue == nullptr || queue->clazz == nullptr) return false;
    DexField* messages_field = queue->clazz->FindInstanceField(
        "mMessages", "Landroid/os/Message;");
    return messages_field != nullptr &&
           queue->GetField<DexObject*>(messages_field->offset_or_slot) != nullptr;
}

DexObject* NewRunnable(const char* descriptor) {
    DexClass* klass = g_linker->FindClass(descriptor);
    if (klass == nullptr || klass->is_stub || !g_interp->EnsureInitialized(klass)) {
        Check(false, "synthetic Runnable class is available");
        g_interp->ClearPendingException();
        return nullptr;
    }
    DexObject* runnable = g_linker->AllocObject(klass);
    DexMethod* ctor = klass->FindDirectMethod("<init>", "()V");
    if (runnable == nullptr || ctor == nullptr) {
        Check(false, "synthetic Runnable and constructor are available");
        return nullptr;
    }
    const DexValue arg = DexValue::Ref(runnable);
    g_interp->ClearPendingException();
    g_interp->Execute(ctor, &arg, 1);
    if (g_interp->HasPendingException()) {
        Check(false, "synthetic Runnable constructor succeeds");
        g_interp->ClearPendingException();
        return nullptr;
    }
    return runnable;
}

DexObject* NewObject(const char* descriptor, const char* ctor_signature,
                    const std::vector<DexValue>& args = {}) {
    DexClass* klass = g_linker->FindClass(descriptor);
    if (klass == nullptr || klass->is_stub || !g_interp->EnsureInitialized(klass)) {
        Check(false, "framework object class is available");
        g_interp->ClearPendingException();
        return nullptr;
    }
    DexObject* object = g_linker->AllocObject(klass);
    DexMethod* ctor = klass->FindDirectMethod("<init>", ctor_signature);
    if (object == nullptr || ctor == nullptr) {
        Check(false, "framework object and constructor are available");
        return nullptr;
    }
    std::vector<DexValue> full_args{DexValue::Ref(object)};
    full_args.insert(full_args.end(), args.begin(), args.end());
    g_interp->ClearPendingException();
    g_interp->Execute(ctor, full_args.data(), full_args.size());
    if (g_interp->HasPendingException()) {
        Check(false, "framework object constructor succeeds");
        g_interp->ClearPendingException();
        return nullptr;
    }
    return object;
}

int SurfaceCallbackValue(DexClass* callback_class, const char* name) {
    DexField* field = callback_class != nullptr
            ? callback_class->FindStaticField(name, "I") : nullptr;
    return field != nullptr
            ? callback_class->static_values[field->offset_or_slot].i : -1;
}

void test_window_and_surface_lifecycle() {
    std::printf("[runtime] Window layout gates SurfaceHolder lifecycle callbacks\n");

    DexObject* activity = NewObject("Landroid/app/Activity;", "()V");
    DexValue window_value;
    Check(activity != nullptr && CallVirtual(
                  activity, "getWindow", "()Landroid/view/Window;", {}, &window_value),
          "Activity creates its Window");
    auto* window = reinterpret_cast<DexObject*>(window_value.l);
    if (window == nullptr) return;
    DexValue decor_before;
    Check(CallVirtual(window, "peekDecorView", "()Landroid/view/View;", {}, &decor_before) &&
                  decor_before.l == nullptr,
          "peekDecorView does not install a decor tree");

    DexValue decor_value;
    Check(CallVirtual(window, "getDecorView", "()Landroid/view/View;", {}, &decor_value),
          "getDecorView installs the Window root");
    auto* decor = reinterpret_cast<DexObject*>(decor_value.l);
    Check(decor != nullptr && decor->clazz != nullptr &&
                  std::string(decor->clazz->descriptor) == "Landroid/view/Window$DecorView;",
          "Window root is its FrameLayout DecorView");
    DexValue content_value;
    Check(CallVirtual(window, "findViewById", "(I)Landroid/view/View;",
                      {DexValue::Int(0x01020002)}, &content_value),
          "Window resolves android.R.id.content");
    auto* content = reinterpret_cast<DexObject*>(content_value.l);
    Check(content != nullptr && content->clazz != nullptr &&
                  std::string(content->clazz->descriptor) == "Landroid/widget/FrameLayout;" &&
                  content != decor,
          "content frame is distinct from DecorView");

    DexObject* surface_view = NewObject(
            "Landroid/view/SurfaceView;", "(Landroid/content/Context;)V",
            {DexValue::Ref(activity)});
    DexObject* surface_callback = g_linker->AllocObject(
            g_linker->FindClass("Lcom/kudroid/SurfaceCallbackProbe;"));
    DexObject* window_callback = g_linker->AllocObject(
            g_linker->FindClass("Lcom/kudroid/SurfaceCallbackProbe;"));
    DexClass* callback_class = g_linker->FindClass("Lcom/kudroid/SurfaceCallbackProbe;");
    Check(surface_view != nullptr && surface_callback != nullptr && window_callback != nullptr &&
                  callback_class != nullptr,
          "SurfaceView and callback fixtures are available");
    if (surface_view == nullptr || surface_callback == nullptr || window_callback == nullptr ||
        callback_class == nullptr) return;
    for (const char* field : {"created", "changed", "redraw", "destroyed", "width", "height", "sequence"}) {
        DexField* value = callback_class->FindStaticField(field, "I");
        if (value != nullptr) callback_class->static_values[value->offset_or_slot] = DexValue::Int(0);
    }

    Check(CallVirtual(window, "setContentView", "(Landroid/view/View;)V",
                      {DexValue::Ref(surface_view)}),
          "Window places content inside android.R.id.content");
    DexValue child_count;
    Check(CallVirtual(window, "setContentView", "(Landroid/view/View;)V",
                      {DexValue::Ref(surface_view)}) &&
                  CallVirtual(content, "getChildCount", "()I", {}, &child_count) &&
                  child_count.i == 1,
          "replacing Window content leaves exactly one child in the content frame");
    DexValue holder_value;
    Check(CallVirtual(surface_view, "getHolder", "()Landroid/view/SurfaceHolder;", {},
                      &holder_value),
          "SurfaceView provides its holder");
    auto* holder = reinterpret_cast<DexObject*>(holder_value.l);
    Check(holder != nullptr && CallVirtual(
                  holder, "addCallback", "(Landroid/view/SurfaceHolder$Callback;)V",
                  {DexValue::Ref(surface_callback)}),
          "SurfaceView registers its callback");
    Check(CallVirtual(window, "takeSurface", "(Landroid/view/SurfaceHolder$Callback2;)V",
                      {DexValue::Ref(window_callback)}),
          "Window registers its surface callback");
    Check(SurfaceCallbackValue(callback_class, "created") == 0 &&
                  SurfaceCallbackValue(callback_class, "changed") == 0 &&
                  SurfaceCallbackValue(callback_class, "redraw") == 0,
          "adding callbacks before layout does not dispatch synchronously");

    Check(CallVirtual(window, "updateSurfaceSize", "(IIZ)V",
                      {DexValue::Int(640), DexValue::Int(360), DexValue::Int(1)}),
          "Window records the ready Metal framebuffer size");
    Check(CallVirtual(window, "dispatchSurfaceReady", "()V") &&
                  CallVirtual(surface_view, "dispatchSurfaceReady", "(II)V",
                              {DexValue::Int(640), DexValue::Int(360)}) &&
                  SurfaceCallbackValue(callback_class, "created") == 0 &&
                  SurfaceCallbackValue(callback_class, "changed") == 0,
          "a ready framebuffer does not dispatch callbacks before view layout");
    DexValue laid_out;
    Check(CallVirtual(window, "measureAndLayout", "()Z", {}, &laid_out) && laid_out.i != 0,
          "Window measures and lays out its decor tree");
    DexValue decor_width;
    DexValue decor_height;
    Check(CallVirtual(decor, "getWidth", "()I", {}, &decor_width) && decor_width.i == 640 &&
                  CallVirtual(decor, "getHeight", "()I", {}, &decor_height) &&
                  decor_height.i == 360,
          "decor bounds match the Metal framebuffer");
    Check(CallVirtual(window, "dispatchSurfaceReady", "()V") &&
                  CallVirtual(surface_view, "dispatchSurfaceReady", "(II)V",
                              {DexValue::Int(640), DexValue::Int(360)}),
          "Window dispatches ready surfaces after layout");
    Check(SurfaceCallbackValue(callback_class, "created") == 2 &&
                  SurfaceCallbackValue(callback_class, "changed") == 2 &&
                  SurfaceCallbackValue(callback_class, "redraw") == 2 &&
                  SurfaceCallbackValue(callback_class, "width") == 640 &&
                  SurfaceCallbackValue(callback_class, "height") == 360 &&
                  SurfaceCallbackValue(callback_class, "sequence") == 123123,
          "create, change, and redraw arrive once with real dimensions");
    DexField* sequence_field = callback_class->FindStaticField("sequence", "I");
    if (sequence_field != nullptr) {
        callback_class->static_values[sequence_field->offset_or_slot] = DexValue::Int(0);
    }

    DexValue surface_value;
    Check(CallVirtual(holder, "getSurface", "()Landroid/view/Surface;", {}, &surface_value),
          "SurfaceHolder exposes its Surface");
    auto* surface = reinterpret_cast<DexObject*>(surface_value.l);
    DexValue valid;
    DexValue surface_width;
    DexValue surface_height;
    Check(surface != nullptr && CallVirtual(surface, "isValid", "()Z", {}, &valid) &&
                  valid.i != 0 && CallVirtual(surface, "getWidth", "()I", {}, &surface_width) &&
                  surface_width.i == 640 && CallVirtual(surface, "getHeight", "()I", {},
                                                        &surface_height) &&
                  surface_height.i == 360,
          "Surface validity and size follow its created state");

    Check(CallVirtual(window, "dispatchSurfaceReady", "()V") &&
                  CallVirtual(surface_view, "dispatchSurfaceReady", "(II)V",
                              {DexValue::Int(640), DexValue::Int(360)}) &&
                  SurfaceCallbackValue(callback_class, "created") == 2 &&
                  SurfaceCallbackValue(callback_class, "changed") == 2 &&
                  SurfaceCallbackValue(callback_class, "redraw") == 2 &&
                  SurfaceCallbackValue(callback_class, "sequence") == 0,
          "an unchanged layout does not redeliver Surface events");

    Check(CallVirtual(window, "updateSurfaceSize", "(IIZ)V",
                      {DexValue::Int(800), DexValue::Int(480), DexValue::Int(1)}) &&
                  CallVirtual(window, "measureAndLayout", "()Z", {}, &laid_out) &&
                  CallVirtual(window, "dispatchSurfaceReady", "()V") &&
                  CallVirtual(surface_view, "dispatchSurfaceReady", "(II)V",
                              {DexValue::Int(800), DexValue::Int(480)}) &&
                  SurfaceCallbackValue(callback_class, "created") == 2 &&
                  SurfaceCallbackValue(callback_class, "changed") == 4 &&
                  SurfaceCallbackValue(callback_class, "redraw") == 4 &&
                  SurfaceCallbackValue(callback_class, "width") == 800 &&
                  SurfaceCallbackValue(callback_class, "height") == 480 &&
                  SurfaceCallbackValue(callback_class, "sequence") == 2323,
          "resize emits change and redraw without recreating the Surface");

    if (sequence_field != nullptr) {
        callback_class->static_values[sequence_field->offset_or_slot] = DexValue::Int(0);
    }
    Check(CallVirtual(window, "dispatchSurfaceDestroyed", "()V") &&
                  CallVirtual(surface_view, "dispatchSurfaceDestroyed", "()V") &&
                  SurfaceCallbackValue(callback_class, "destroyed") == 2 &&
                  SurfaceCallbackValue(callback_class, "sequence") == 44,
          "Window and SurfaceView destroy their active surfaces once");
    Check(CallVirtual(surface, "isValid", "()Z", {}, &valid) && valid.i == 0 &&
                  CallVirtual(surface, "getWidth", "()I", {}, &surface_width) &&
                  surface_width.i == 0 && CallVirtual(surface, "getHeight", "()I", {},
                                                     &surface_height) &&
                  surface_height.i == 0,
          "destroyed Surface is invalid and has no stale dimensions");
}

void test_view_post_uses_main_queue() {
    std::printf("[runtime] View callbacks post asynchronously to the main queue\n");
    Check(CallStatic("Landroid/os/Looper;", "prepareMainLooper", "()V"),
          "main Looper is prepared for View callbacks");
    DexValue main_value;
    if (!CallStatic("Landroid/os/Looper;", "getMainLooper",
                    "()Landroid/os/Looper;", &main_value)) return;
    auto* main_looper = reinterpret_cast<DexObject*>(main_value.l);

    DexClass* view_class = g_linker->FindClass("Landroid/view/View;");
    DexObject* view = view_class != nullptr ? g_linker->AllocObject(view_class) : nullptr;
    DexObject* recorder = NewRunnable("Lcom/kudroid/RecordingRunnable;");
    DexObject* quit = NewRunnable("Lcom/kudroid/TeardownRunnable;");
    DexClass* recorder_class = g_linker->FindClass("Lcom/kudroid/RecordingRunnable;");
    DexField* ran_field = recorder_class != nullptr
                              ? recorder_class->FindStaticField("sRan", "Z")
                              : nullptr;
    Check(view != nullptr && recorder != nullptr && quit != nullptr && ran_field != nullptr,
          "View and callback fixtures are available");
    if (view == nullptr || recorder == nullptr || quit == nullptr || ran_field == nullptr) {
        return;
    }
    recorder_class->static_values[ran_field->offset_or_slot] = DexValue::Int(0);

    DexValue posted;
    Check(CallVirtual(view, "post", "(Ljava/lang/Runnable;)Z",
                      {DexValue::Ref(recorder)}, &posted) && posted.i != 0,
          "View.post accepts a Runnable");
    Check(recorder_class->static_values[ran_field->offset_or_slot].i == 0,
          "View.post does not run the callback inline");
    Check(QueueHasMessages(main_looper), "View.post enqueues work on the main Looper");

    DexValue delayed;
    const bool delayed_api = CallVirtual(
        view, "postDelayed", "(Ljava/lang/Runnable;J)Z",
        {DexValue::Ref(quit), DexValue::Long(60)}, &delayed);
    Check(delayed_api && delayed.i != 0, "View.postDelayed accepts delayed work");
    const bool removed = CallVirtual(view, "removeCallbacks", "(Ljava/lang/Runnable;)V",
                                     {DexValue::Ref(quit)});
    Check(removed, "View.removeCallbacks removes the delayed Runnable");
    DexObject* queue = QueueObject(main_looper);
    DexField* messages_field = queue != nullptr
                                   ? queue->clazz->FindInstanceField(
                                         "mMessages", "Landroid/os/Message;")
                                   : nullptr;
    DexObject* head = messages_field != nullptr
                          ? queue->GetField<DexObject*>(messages_field->offset_or_slot)
                          : nullptr;
    DexField* callback_field = head != nullptr
                                   ? head->clazz->FindInstanceField(
                                         "callback", "Ljava/lang/Runnable;")
                                   : nullptr;
    DexField* next_field = head != nullptr
                               ? head->clazz->FindInstanceField(
                                     "next", "Landroid/os/Message;")
                               : nullptr;
    Check(callback_field != nullptr && next_field != nullptr &&
              head->GetField<DexObject*>(callback_field->offset_or_slot) == recorder &&
              head->GetField<DexObject*>(next_field->offset_or_slot) == nullptr,
          "removeCallbacks leaves only the unrelated queued action");

    const bool quit_posted = CallVirtual(view, "post", "(Ljava/lang/Runnable;)Z",
                                         {DexValue::Ref(quit)}, &posted);
    Check(quit_posted && posted.i != 0, "View can enqueue a teardown callback");
    if (QueueHasMessages(main_looper)) {
        Check(CallStatic("Landroid/os/Looper;", "loop", "()V"),
              "main loop dispatches the queued View callbacks");
        Check(recorder_class->static_values[ran_field->offset_or_slot].i != 0,
              "queued View callback runs when the Looper dispatches it");
    } else {
        // Keep a red test from leaking a native queue slot if an old inline
        // implementation already called quitForTeardown on this thread.
        DexObject* queue = QueueObject(main_looper);
        if (queue != nullptr) {
            CallVirtual(queue, "disposeAfterLoop", "()V");
        }
        Check(false, "queued View callbacks are available for loop dispatch");
    }
}

void test_activity_run_on_ui_thread_dispatch() {
    std::printf("[runtime] Activity.runOnUiThread preserves UI-thread dispatch\n");
    Check(CallStatic("Landroid/os/Looper;", "prepareMainLooper", "()V"),
          "main Looper is prepared for Activity callbacks");
    DexValue main_value;
    DexValue current_thread_value;
    if (!CallStatic("Landroid/os/Looper;", "getMainLooper",
                    "()Landroid/os/Looper;", &main_value) ||
        !CallStatic("Ljava/lang/Thread;", "currentThread", "()Ljava/lang/Thread;",
                    &current_thread_value)) return;
    auto* main_looper = reinterpret_cast<DexObject*>(main_value.l);

    DexClass* activity_class = g_linker->FindClass("Landroid/app/Activity;");
    DexClass* thread_class = g_linker->FindClass("Ljava/lang/Thread;");
    DexObject* activity = activity_class != nullptr
                              ? g_linker->AllocObject(activity_class)
                              : nullptr;
    DexObject* worker_thread = thread_class != nullptr
                                   ? g_linker->AllocObject(thread_class)
                                   : nullptr;
    DexClass* handler_class = g_linker->FindClass("Landroid/os/Handler;");
    DexObject* handler = handler_class != nullptr
                             ? g_linker->AllocObject(handler_class)
                             : nullptr;
    DexObject* recorder = NewRunnable("Lcom/kudroid/RecordingRunnable;");
    DexObject* quit = NewRunnable("Lcom/kudroid/TeardownRunnable;");
    DexClass* recorder_class = g_linker->FindClass("Lcom/kudroid/RecordingRunnable;");
    DexField* ran_field = recorder_class != nullptr
                              ? recorder_class->FindStaticField("sRan", "Z")
                              : nullptr;
    DexField* ui_thread_field = activity_class != nullptr
                                    ? activity_class->FindInstanceField(
                                          "mUiThread", "Ljava/lang/Thread;")
                                    : nullptr;
    DexField* ui_handler_field = activity_class != nullptr
                                     ? activity_class->FindInstanceField(
                                           "mUiHandler", "Landroid/os/Handler;")
                                     : nullptr;
    DexMethod* handler_ctor = handler_class != nullptr
                                  ? handler_class->FindDirectMethod(
                                        "<init>", "(Landroid/os/Looper;)V")
                                  : nullptr;
    Check(activity != nullptr && worker_thread != nullptr && handler != nullptr &&
              recorder != nullptr && quit != nullptr &&
              ran_field != nullptr && ui_thread_field != nullptr &&
              ui_handler_field != nullptr && handler_ctor != nullptr,
          "Activity callback fixtures and UI binding fields are available");
    if (activity == nullptr || worker_thread == nullptr || handler == nullptr ||
        recorder == nullptr || quit == nullptr ||
        ran_field == nullptr || ui_thread_field == nullptr || ui_handler_field == nullptr ||
        handler_ctor == nullptr) return;

    const DexValue handler_args[] = {DexValue::Ref(handler), main_value};
    g_interp->ClearPendingException();
    g_interp->Execute(handler_ctor, handler_args, 2);
    Check(!g_interp->HasPendingException(), "Handler binds to the main Looper");
    if (g_interp->HasPendingException()) {
        g_interp->ClearPendingException();
        return;
    }
    activity->SetField<DexObject*>(ui_handler_field->offset_or_slot, handler);
    activity->SetField<DexObject*>(ui_thread_field->offset_or_slot, worker_thread);
    recorder_class->static_values[ran_field->offset_or_slot] = DexValue::Int(0);

    const bool posted = CallVirtual(activity, "runOnUiThread", "(Ljava/lang/Runnable;)V",
                                    {DexValue::Ref(recorder)});
    Check(posted, "off-UI runOnUiThread call succeeds");
    Check(recorder_class->static_values[ran_field->offset_or_slot].i == 0,
          "off-UI runOnUiThread does not execute inline");
    Check(QueueHasMessages(main_looper), "off-UI runOnUiThread posts to main");
    const bool quit_posted = CallVirtual(activity, "runOnUiThread",
                                         "(Ljava/lang/Runnable;)V",
                                         {DexValue::Ref(quit)});
    Check(quit_posted, "off-UI teardown callback is accepted");
    if (QueueHasMessages(main_looper)) {
        Check(CallStatic("Landroid/os/Looper;", "loop", "()V"),
              "main loop dispatches Activity callbacks");
        Check(recorder_class->static_values[ran_field->offset_or_slot].i != 0,
              "off-UI Activity callback runs on the main Looper");
    } else {
        DexObject* queue = QueueObject(main_looper);
        if (queue != nullptr) CallVirtual(queue, "disposeAfterLoop", "()V");
        Check(false, "Activity callback remains queued for main-loop dispatch");
    }

    Check(CallStatic("Landroid/os/Looper;", "prepareMainLooper", "()V"),
          "a fresh main Looper is prepared for inline dispatch");
    DexValue inline_looper_value;
    if (!CallStatic("Landroid/os/Looper;", "getMainLooper",
                    "()Landroid/os/Looper;", &inline_looper_value)) return;
    DexObject* inline_activity = g_linker->AllocObject(activity_class);
    DexObject* inline_handler = g_linker->AllocObject(handler_class);
    const DexValue inline_handler_args[] = {DexValue::Ref(inline_handler), inline_looper_value};
    g_interp->ClearPendingException();
    g_interp->Execute(handler_ctor, inline_handler_args, 2);
    if (g_interp->HasPendingException()) {
        Check(false, "inline Handler binds to the fresh main Looper");
        g_interp->ClearPendingException();
        return;
    }
    inline_activity->SetField<DexObject*>(ui_handler_field->offset_or_slot, inline_handler);
    inline_activity->SetField<DexObject*>(ui_thread_field->offset_or_slot,
                                          reinterpret_cast<DexObject*>(current_thread_value.l));
    recorder_class->static_values[ran_field->offset_or_slot] = DexValue::Int(0);
    Check(CallVirtual(inline_activity, "runOnUiThread", "(Ljava/lang/Runnable;)V",
                      {DexValue::Ref(recorder)}),
          "UI-thread runOnUiThread call succeeds");
    Check(recorder_class->static_values[ran_field->offset_or_slot].i != 0,
          "UI-thread runOnUiThread executes inline");
    DexObject* inline_queue = QueueObject(reinterpret_cast<DexObject*>(inline_looper_value.l));
    if (inline_queue != nullptr) {
        CallVirtual(inline_queue, "quitInternal", "()V");
        Check(CallStatic("Landroid/os/Looper;", "loop", "()V"),
              "inline Activity test disposes the main queue");
    }
}

void test_looper_queue_release_and_relaunch() {
    std::printf("[runtime] a quit main Looper releases its queue and can relaunch\n");

    Check(CallStatic("Landroid/os/Looper;", "prepareMainLooper", "()V"),
          "first main Looper is prepared");
    DexValue old_value;
    const bool got_old = CallStatic("Landroid/os/Looper;", "getMainLooper",
                                    "()Landroid/os/Looper;", &old_value);
    Check(got_old && old_value.l != nullptr, "first main Looper is available");
    if (!got_old || old_value.l == nullptr) return;

    auto* old_looper = reinterpret_cast<DexObject*>(old_value.l);
    int64_t old_ptr = 0;
    Check(QueuePtr(old_looper, &old_ptr) && old_ptr != 0,
          "first Looper owns a native queue slot");
    Check(old_ptr != 0 && kudroid_looper_is_main(old_ptr) != 0,
          "first queue slot is registered as the native main slot");

    auto post_teardown = [] {
        DexClass* handler_class = g_linker->FindClass("Landroid/os/Handler;");
        DexClass* runnable_class = g_linker->FindClass("Lcom/kudroid/TeardownRunnable;");
        if (handler_class == nullptr || runnable_class == nullptr ||
            !g_interp->EnsureInitialized(handler_class) ||
            !g_interp->EnsureInitialized(runnable_class)) {
            Check(false, "teardown callback classes are available");
            g_interp->ClearPendingException();
            return false;
        }
        DexObject* handler = g_linker->AllocObject(handler_class);
        DexObject* runnable = g_linker->AllocObject(runnable_class);
        DexMethod* handler_ctor = handler_class->FindDirectMethod("<init>", "()V");
        DexMethod* runnable_ctor = runnable_class->FindDirectMethod("<init>", "()V");
        if (handler == nullptr || runnable == nullptr || handler_ctor == nullptr ||
            runnable_ctor == nullptr) {
            Check(false, "teardown callback objects and constructors are available");
            return false;
        }
        const DexValue handler_arg = DexValue::Ref(handler);
        const DexValue runnable_arg = DexValue::Ref(runnable);
        g_interp->ClearPendingException();
        g_interp->Execute(handler_ctor, &handler_arg, 1);
        g_interp->Execute(runnable_ctor, &runnable_arg, 1);
        if (g_interp->HasPendingException()) {
            Check(false, "teardown callback constructors succeed");
            g_interp->ClearPendingException();
            return false;
        }
        DexValue posted;
        if (!CallVirtual(handler, "post", "(Ljava/lang/Runnable;)Z",
                         {DexValue::Ref(runnable)}, &posted)) {
            return false;
        }
        Check(posted.i != 0, "teardown callback is queued on the Looper");
        return posted.i != 0;
    };
    if (!post_teardown()) return;
    Check(CallStatic("Landroid/os/Looper;", "loop", "()V"),
          "the teardown loop returns");

    int64_t released_ptr = -1;
    Check(QueuePtr(old_looper, &released_ptr) && released_ptr == 0,
          "the old queue clears its native pointer after loop exit");
    Check(kudroid_looper_is_main(old_ptr) == 0,
          "the old native queue is no longer the main slot");

    void* previous_looper = old_value.l;
    for (int relaunch = 0; relaunch < 5; ++relaunch) {
        Check(CallStatic("Landroid/os/Looper;", "resetMainLooperForRelaunch", "()V"),
              "relaunch reset clears the old Java Looper");
        Check(CallStatic("Landroid/os/Looper;", "prepareMainLooper", "()V"),
              "a main Looper can be prepared again on the same thread");
        DexValue new_value;
        const bool got_new = CallStatic("Landroid/os/Looper;", "getMainLooper",
                                        "()Landroid/os/Looper;", &new_value);
        Check(got_new && new_value.l != nullptr && new_value.l != previous_looper,
              "relaunch creates a distinct main Looper");
        if (!got_new || new_value.l == nullptr) return;

        DexValue current_value;
        const bool got_current = CallStatic("Landroid/os/Looper;", "myLooper",
                                            "()Landroid/os/Looper;", &current_value);
        Check(got_current && current_value.l == new_value.l,
              "current and main Looper agree after relaunch");
        auto* new_looper = reinterpret_cast<DexObject*>(new_value.l);
        int64_t new_ptr = 0;
        Check(QueuePtr(new_looper, &new_ptr) && new_ptr != 0,
              "relaunch creates a fresh native queue slot");
        Check(new_ptr != 0 && kudroid_looper_is_main(new_ptr) != 0,
              "the fresh queue is registered as the native main slot");
        if (!post_teardown()) return;
        Check(CallStatic("Landroid/os/Looper;", "loop", "()V"),
              "the relaunch teardown loop returns");
        int64_t new_released_ptr = -1;
        Check(QueuePtr(new_looper, &new_released_ptr) && new_released_ptr == 0,
              "each relaunch releases its native queue slot");
        Check(kudroid_looper_is_main(new_ptr) == 0,
              "each retired queue leaves the native main slot");
        previous_looper = new_value.l;
    }
}

void test_run_completion_barrier() {
    std::printf("[runtime] launch and stop share a serialized run-state barrier\n");
    Check(!request_apk_stop(), "stop request is ignored when no run is active");
    Check(begin_apk_run(std::chrono::milliseconds(0)) == ApkRunStartResult::Acquired,
          "first run acquires the slot");
    Check(begin_apk_run(std::chrono::milliseconds(0)) == ApkRunStartResult::AlreadyActive,
          "active run rejects a duplicate launch");
    Check(request_apk_stop(), "first stop request latches stopping");
    Check(!request_apk_stop(), "duplicate stop request is ignored");
    Check(begin_apk_run(std::chrono::milliseconds(5)) ==
              ApkRunStartResult::TeardownTimedOut,
          "launch times out rather than overlapping a stopping run");
    complete_apk_run();

    Check(begin_apk_run(std::chrono::milliseconds(0)) == ApkRunStartResult::Acquired,
          "run slot can be acquired after completion");
    Check(request_apk_stop(), "second run can latch a stop");
    std::thread finisher([] {
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        complete_apk_run();
    });
    const ApkRunStartResult waited =
        begin_apk_run(std::chrono::milliseconds(250));
    finisher.join();
    Check(waited == ApkRunStartResult::Acquired,
          "launch wakes and acquires after teardown completion");
    Check(s_isApkRunning.load() && !s_stopping.load(),
          "new run starts active with its stopping latch cleared");
    complete_apk_run();
}

}  // namespace

int main() {
    std::printf("=== KuART relaunch lifecycle ===\n");
    DexClassLinker linker;
    std::string error;
    const std::vector<uint8_t> test_dex = BuildTeardownRunnableDex();
    const std::vector<uint8_t> surface_dex = BuildSurfaceCallbackDex();
    if (!linker.AddDexFile(g_framework_dex_bytes, g_framework_dex_size,
                           "framework.dex", &error)) {
        std::printf("  FAIL AddDexFile: %s\n", error.c_str());
        return 1;
    }
    if (!linker.AddDexFile(test_dex.data(), test_dex.size(), "test.dex", &error)) {
        std::printf("  FAIL AddDexFile(test.dex): %s\n", error.c_str());
        return 1;
    }
    if (!linker.AddDexFile(surface_dex.data(), surface_dex.size(), "surface-test.dex", &error)) {
        std::printf("  FAIL AddDexFile(surface-test.dex): %s\n", error.c_str());
        return 1;
    }
    Interpreter interp(&linker);
    DexJniEnv jni(&linker, &interp);
    interp.set_jni_env(&jni);
    g_linker = &linker;
    g_interp = &interp;

    test_looper_queue_release_and_relaunch();
    test_window_and_surface_lifecycle();
    test_view_post_uses_main_queue();
    test_activity_run_on_ui_thread_dispatch();
    test_run_completion_barrier();
    std::printf("=== %d checks, %d failures ===\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
