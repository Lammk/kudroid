// test_kuart_camera.cpp - android.hardware.Camera against a fake host capture session,
// run through the real framework.dex: permission gate, session control, NV21 preview
// frames (with and without callback buffers), photos, and the frame orientation contract.
#include "dex_builder.h"
#include "kudroid/framework_dex_bytes.h"
#include "kudroid/kudroid_bridge.h"
#include "kudroid/kuart/DexClassLinker.h"
#include "kudroid/kuart/DexJniEnv.h"
#include "kudroid/kuart/DexObject.h"
#include "kudroid/kuart/DexString.h"
#include "kudroid/kuart/Interpreter.h"
#include "kudroid/platform/CameraShim.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

using dexbuild::ClassSpec;
using dexbuild::DexBuilder;
using dexbuild::FieldSpec;
using dexbuild::MethodSpec;
using kudroid::kuart::DexArray;
using kudroid::kuart::DexClass;
using kudroid::kuart::DexClassLinker;
using kudroid::kuart::DexField;
using kudroid::kuart::DexJniEnv;
using kudroid::kuart::DexMethod;
using kudroid::kuart::DexObject;
using kudroid::kuart::DexValue;
using kudroid::kuart::Interpreter;

int g_checks = 0;
int g_failures = 0;
DexClassLinker* g_linker = nullptr;
Interpreter* g_interp = nullptr;

constexpr const char* kCameraPermission = "android.permission.CAMERA";

void Check(bool condition, const char* message) {
    ++g_checks;
    std::printf("%s %s\n", condition ? "  OK  " : "  FAIL", message);
    if (!condition) ++g_failures;
}

// ── a Java class that records what the camera delivers ──────────────────────
//
// Implements PreviewCallback and PictureCallback: each callback bumps a static counter
// and stores the byte[] it was given.

constexpr uint8_t kOpReturnVoid = 0x0e;
constexpr uint8_t kOpSget = 0x60;
constexpr uint8_t kOpSput = 0x67;
constexpr uint8_t kOpSputObject = 0x69;
constexpr uint8_t kOpAddIntLit8 = 0xd8;
constexpr uint32_t kAccPublic = 0x1;
constexpr uint32_t kAccStatic = 0x8;
constexpr uint32_t kAccConstructor = 0x10000;

uint16_t Op11x(uint8_t op, uint8_t a) { return static_cast<uint16_t>(op | (a << 8)); }
void Op21c(std::vector<uint16_t>* code, uint8_t op, uint8_t a, uint16_t idx) {
    code->push_back(static_cast<uint16_t>(op | (a << 8)));
    code->push_back(idx);
}

struct RecorderSpecs {
    FieldSpec frame_count{"frameCount", "I", kAccPublic | kAccStatic};
    FieldSpec last_frame{"lastFrame", "[B", kAccPublic | kAccStatic};
    FieldSpec photo_count{"photoCount", "I", kAccPublic | kAccStatic};
    FieldSpec last_photo{"lastPhoto", "[B", kAccPublic | kAccStatic};
    MethodSpec ctor;
    MethodSpec on_frame;
    MethodSpec on_photo;

    RecorderSpecs() {
        ctor.name = "<init>";
        ctor.access_flags = kAccPublic | kAccConstructor;
        on_frame.name = "onPreviewFrame";
        on_frame.params = {"[B", "Landroid/hardware/Camera;"};
        on_photo.name = "onPictureTaken";
        on_photo.params = {"[B", "Landroid/hardware/Camera;"};
    }
};

std::vector<ClassSpec> RecorderClasses(const RecorderSpecs& s) {
    ClassSpec cb;
    cb.descriptor = "Lcom/foo/CamRecorder;";
    cb.interfaces = {"Landroid/hardware/Camera$PreviewCallback;",
                     "Landroid/hardware/Camera$PictureCallback;"};
    cb.static_fields = {s.frame_count, s.last_frame, s.photo_count, s.last_photo};
    cb.direct_methods = {s.ctor};
    cb.virtual_methods = {s.on_frame, s.on_photo};
    return {cb};
}

// onX(this=v1, data=v2, camera=v3): lastX = data; countX++.
void FillRecorder(MethodSpec* m, uint16_t last_field, uint16_t count_field) {
    std::vector<uint16_t> c;
    Op21c(&c, kOpSputObject, 2, last_field);
    Op21c(&c, kOpSget, 0, count_field);
    c.push_back(static_cast<uint16_t>(kOpAddIntLit8 | (0 << 8)));
    c.push_back(static_cast<uint16_t>(0 | (1 << 8)));  // v0 = v0 + 1
    Op21c(&c, kOpSput, 0, count_field);
    c.push_back(Op11x(kOpReturnVoid, 0));
    m->code = c;
    m->registers_size = 4;
    m->ins_size = 3;
}

// ── interpreter helpers ─────────────────────────────────────────────────────

DexValue Str(const char* value) {
    return DexValue::Ref(reinterpret_cast<DexObject*>(g_linker->NewString(value)));
}

bool Run(DexMethod* method, std::vector<DexValue> args, DexValue* result, bool* threw) {
    if (method == nullptr) return false;
    g_interp->ClearPendingException();
    const DexValue value = g_interp->Execute(method, args.data(), args.size());
    const bool exception = g_interp->HasPendingException();
    if (exception) g_interp->ClearPendingException();
    if (threw != nullptr) *threw = exception;
    if (!exception && result != nullptr) *result = value;
    return !exception;
}

bool CallStatic(const char* descriptor, const char* name, const char* sig,
                std::vector<DexValue> args, DexValue* result, bool* threw = nullptr) {
    DexClass* klass = g_linker->FindClass(descriptor);
    if (klass == nullptr || klass->is_stub || !g_interp->EnsureInitialized(klass)) return false;
    DexMethod* method = klass->FindDirectMethod(name, sig);
    if (method == nullptr) method = klass->FindVirtualMethod(name, sig);
    return Run(method, std::move(args), result, threw);
}

bool CallVirtual(DexObject* receiver, const char* name, const char* sig,
                 std::vector<DexValue> args, DexValue* result, bool* threw = nullptr) {
    if (receiver == nullptr) return false;
    DexMethod* method = receiver->clazz->FindVirtualMethod(name, sig);
    if (method == nullptr) method = receiver->clazz->FindDirectMethod(name, sig);
    args.insert(args.begin(), DexValue::Ref(receiver));
    return Run(method, std::move(args), result, threw);
}

DexObject* NewObject(const char* descriptor, const char* sig, std::vector<DexValue> args) {
    DexClass* klass = g_linker->FindClass(descriptor);
    if (klass == nullptr || klass->is_stub || !g_interp->EnsureInitialized(klass)) return nullptr;
    DexObject* object = g_linker->AllocObject(klass);
    DexMethod* ctor = klass->FindDirectMethod("<init>", sig);
    if (object == nullptr || ctor == nullptr) return nullptr;
    args.insert(args.begin(), DexValue::Ref(object));
    return Run(ctor, args, nullptr, nullptr) ? object : nullptr;
}

DexArray* NewBytes(int count) {
    DexClass* klass = g_linker->FindClass("[B");
    return klass != nullptr ? g_linker->AllocArray(klass, count) : nullptr;
}

DexValue& Static(const char* name, const char* type) {
    DexClass* klass = g_linker->FindClass("Lcom/foo/CamRecorder;");
    return klass->static_values[klass->FindStaticField(name, type)->offset_or_slot];
}

constexpr const char* kPreviewCbSig = "(Landroid/hardware/Camera$PreviewCallback;)V";

// ── the fake host ───────────────────────────────────────────────────────────

struct FakeHost {
    int opened = -1;
    int closed = 0;
    int started = 0;
    int stopped = 0;
    int startWidth = 0;
    int startHeight = 0;
    int pictures = 0;
    int previewVisible = -1;
    int previewRotation = 0;
};
FakeHost g_host;

int HostCount() { return 2; }
int HostInfo(int id, int* facing, int* orientation) {
    *facing = id == 0 ? 0 : 1;
    *orientation = id == 0 ? 90 : 270;
    return 0;
}
int HostOpen(int id) { g_host.opened = id; return 0; }
void HostClose() { ++g_host.closed; }
int HostStart(int w, int h) {
    ++g_host.started;
    g_host.startWidth = w;
    g_host.startHeight = h;
    return 0;
}
void HostStop() { ++g_host.stopped; }
int HostTakePicture() { ++g_host.pictures; return 0; }
void HostSetPreview(int visible, int rotation) {
    g_host.previewVisible = visible;
    g_host.previewRotation = rotation;
}

const KuDroidCameraHost kHost = {HostCount, HostInfo, HostOpen, HostClose,
                                 HostStart, HostStop, HostTakePicture, HostSetPreview};

// A 4x2 NV12 frame: Y = 0..7, and two Cb/Cr pairs.
void PushFrame(int rotate180) {
    const uint8_t y[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    const uint8_t cbcr[4] = {100, 200, 101, 201};  // (Cb 100, Cr 200), (Cb 101, Cr 201)
    kudroid_camera_push_frame(y, 4, cbcr, 4, 4, 2, rotate180);
}

void Dispatch() { CallStatic("Landroid/hardware/Camera;", "dispatchPending", "()V", {}, nullptr); }

bool NV21Matches(DexArray* data) {
    if (data == nullptr || data->length != 12) return false;
    bool ok = true;
    for (int i = 0; i < 8; ++i) ok = ok && data->Get<int8_t>(i) == i;
    return ok && static_cast<uint8_t>(data->Get<int8_t>(8)) == 200 &&
           static_cast<uint8_t>(data->Get<int8_t>(9)) == 100 &&
           static_cast<uint8_t>(data->Get<int8_t>(10)) == 201 &&
           static_cast<uint8_t>(data->Get<int8_t>(11)) == 101;
}

void test_native_frame_contract() {
    std::printf("[native] frames are stored as NV21 in the reported orientation\n");
    PushFrame(0);
    uint8_t out[12] = {};
    Check(kudroid_camera_has_pending() == 1, "a pushed frame is announced to the main Looper");
    Check(bionic_kudroid_camera_events() == 1 && kudroid_camera_has_pending() == 0,
          "reading the events consumes the announcement");
    Check(bionic_kudroid_camera_frame_size() == ((4 << 16) | 2) &&
              bionic_kudroid_camera_copy_frame(out, 12) == 12,
          "a frame is stored at its own size");
    bool y = true;
    for (int i = 0; i < 8; ++i) y = y && out[i] == i;
    Check(y && out[8] == 200 && out[9] == 100 && out[10] == 201 && out[11] == 101,
          "luma is copied and each Cb/Cr pair becomes Cr/Cb");
    Check(bionic_kudroid_camera_copy_frame(out, 11) == 0, "a destination that is too small gets nothing");

    PushFrame(1);
    bionic_kudroid_camera_events();
    bionic_kudroid_camera_copy_frame(out, 12);
    y = true;
    for (int i = 0; i < 8; ++i) y = y && out[i] == 7 - i;
    Check(y && out[8] == 201 && out[9] == 101 && out[10] == 200 && out[11] == 100,
          "a rotate180 frame reverses luma and the chroma pairs");

    kudroid_camera_push_frame(nullptr, 0, nullptr, 0, 4, 2, 0);
    Check(kudroid_camera_has_pending() == 0, "an invalid frame is ignored");

    const uint8_t jpeg[5] = {0xFF, 0xD8, 1, 2, 0xD9};
    kudroid_camera_push_photo(jpeg, 5);
    uint8_t photo[5] = {};
    Check(bionic_kudroid_camera_events() == 2 && bionic_kudroid_camera_photo_size() == 5 &&
              bionic_kudroid_camera_copy_photo(photo, 5) == 5 && std::memcmp(photo, jpeg, 5) == 0,
          "a photo is delivered byte for byte");

    kudroid_camera_report_error(100);
    Check(bionic_kudroid_camera_events() == 4 && bionic_kudroid_camera_take_error() == 100 &&
              bionic_kudroid_camera_take_error() == 0,
          "a capture error is reported once");
}

void test_camera_api(DexObject* recorder) {
    std::printf("[camera] android.hardware.Camera through the host session\n");
    DexValue v;
    bool threw = false;

    kudroid_set_camera_host(nullptr);
    CallStatic("Landroid/hardware/Camera;", "getNumberOfCameras", "()I", {}, &v);
    Check(v.i == 0, "no host session means no cameras");
    kudroid_set_camera_host(&kHost);
    CallStatic("Landroid/hardware/Camera;", "getNumberOfCameras", "()I", {}, &v);
    Check(v.i == 2, "the host's cameras are reported");

    DexObject* info = NewObject("Landroid/hardware/Camera$CameraInfo;", "()V", {});
    CallStatic("Landroid/hardware/Camera;", "getCameraInfo",
               "(ILandroid/hardware/Camera$CameraInfo;)V", {DexValue::Int(1), DexValue::Ref(info)},
               nullptr);
    DexClass* infoClass = info->clazz;
    const int facing = info->GetField<int32_t>(infoClass->FindInstanceField("facing", "I")->offset_or_slot);
    const int orientation =
        info->GetField<int32_t>(infoClass->FindInstanceField("orientation", "I")->offset_or_slot);
    Check(facing == 1 && orientation == 270, "camera info carries facing and orientation");

    kudroid_set_host_authorization(1, 0);
    CallStatic("Landroid/hardware/Camera;", "open", "(I)Landroid/hardware/Camera;",
               {DexValue::Int(0)}, &v, &threw);
    Check(threw && g_host.opened == -1,
          "opening without CAMERA permission fails before the host is touched");

    kudroid_set_host_authorization(1, 1);
    kudroid_prompt_permission_request("", kCameraPermission, 30, nullptr);
    CallStatic("Landroid/hardware/Camera;", "open", "(I)Landroid/hardware/Camera;",
               {DexValue::Int(0)}, &v, &threw);
    DexObject* camera = threw ? nullptr : v.l;
    Check(camera != nullptr && g_host.opened == 0, "an authorized, granted app opens the camera");

    CallVirtual(camera, "setPreviewDisplay", "(Landroid/view/SurfaceHolder;)V",
                {DexValue::Ref(nullptr)}, nullptr);
    Check(g_host.previewVisible == 0, "a null preview display hides the host preview");
    CallVirtual(camera, "setDisplayOrientation", "(I)V", {DexValue::Int(45)}, nullptr, &threw);
    Check(threw, "a display orientation that is not a right angle is rejected");

    // Preview size.
    DexValue params;
    CallVirtual(camera, "getParameters", "()Landroid/hardware/Camera$Parameters;", {}, &params);
    CallVirtual(params.l, "setPreviewSize", "(II)V", {DexValue::Int(123), DexValue::Int(45)},
                nullptr, &threw);
    Check(threw, "an unsupported preview size is rejected");
    CallVirtual(camera, "startPreview", "()V", {}, nullptr);
    Check(g_host.started == 1 && g_host.startWidth == 640 && g_host.startHeight == 480,
          "startPreview starts the host session at the default preview size");
    CallVirtual(params.l, "setPreviewSize", "(II)V", {DexValue::Int(1280), DexValue::Int(720)}, nullptr);
    CallVirtual(camera, "setParameters", "(Landroid/hardware/Camera$Parameters;)V", {params}, nullptr);
    CallVirtual(camera, "stopPreview", "()V", {}, nullptr);
    CallVirtual(camera, "startPreview", "()V", {}, nullptr);
    Check(g_host.startWidth == 1280 && g_host.startHeight == 720 && g_host.stopped == 1,
          "setParameters changes the size used by the next start");

    // Frames into app-supplied buffers: the frame lands in the app's own array.
    Static("frameCount", "I") = DexValue::Int(0);
    DexArray* buffer = NewBytes(12);
    CallVirtual(camera, "setPreviewCallbackWithBuffer", kPreviewCbSig, {DexValue::Ref(recorder)}, nullptr);
    PushFrame(0);
    Dispatch();
    Check(Static("frameCount", "I").i == 0, "with no buffer supplied the frame is dropped");
    CallVirtual(camera, "addCallbackBuffer", "([B)V",
                {DexValue::Ref(reinterpret_cast<DexObject*>(buffer))}, nullptr);
    PushFrame(0);
    Dispatch();
    Check(Static("frameCount", "I").i == 1 &&
              Static("lastFrame", "[B").l == reinterpret_cast<DexObject*>(buffer) && NV21Matches(buffer),
          "a supplied buffer receives the NV21 frame");
    PushFrame(0);
    Dispatch();
    Check(Static("frameCount", "I").i == 1, "a buffer is used once until the app returns it");

    // Frames without buffers reuse one array.
    CallVirtual(camera, "setPreviewCallback", kPreviewCbSig, {DexValue::Ref(recorder)}, nullptr);
    PushFrame(0);
    Dispatch();
    DexObject* first = Static("lastFrame", "[B").l;
    PushFrame(0);
    Dispatch();
    Check(Static("frameCount", "I").i == 3 && Static("lastFrame", "[B").l == first &&
              NV21Matches(reinterpret_cast<DexArray*>(first)),
          "without buffers every frame reuses one array");

    // One-shot.
    CallVirtual(camera, "setOneShotPreviewCallback", kPreviewCbSig, {DexValue::Ref(recorder)}, nullptr);
    PushFrame(0);
    Dispatch();
    PushFrame(0);
    Dispatch();
    Check(Static("frameCount", "I").i == 4, "a one-shot callback receives exactly one frame");

    // Photos stop the preview, as on Android, and arrive once.
    Static("photoCount", "I") = DexValue::Int(0);
    CallVirtual(camera, "setPreviewCallback", kPreviewCbSig, {DexValue::Ref(recorder)}, nullptr);
    CallVirtual(camera, "takePicture",
                "(Landroid/hardware/Camera$ShutterCallback;Landroid/hardware/Camera$PictureCallback;"
                "Landroid/hardware/Camera$PictureCallback;)V",
                {DexValue::Ref(nullptr), DexValue::Ref(nullptr), DexValue::Ref(recorder)}, nullptr);
    Check(g_host.pictures == 1, "takePicture asks the host for a photo");
    const uint8_t jpeg[5] = {0xFF, 0xD8, 1, 2, 0xD9};
    kudroid_camera_push_photo(jpeg, 5);
    Dispatch();
    DexArray* photo = reinterpret_cast<DexArray*>(Static("lastPhoto", "[B").l);
    Check(Static("photoCount", "I").i == 1 && photo != nullptr && photo->length == 5 &&
              static_cast<uint8_t>(photo->Get<int8_t>(0)) == 0xFF && photo->Get<int8_t>(2) == 1,
          "the JPEG reaches the picture callback");
    const int before = Static("frameCount", "I").i;
    PushFrame(0);
    Dispatch();
    Check(Static("frameCount", "I").i == before, "frames stop after takePicture until the preview restarts");
    CallVirtual(camera, "startPreview", "()V", {}, nullptr);
    PushFrame(0);
    Dispatch();
    Check(Static("frameCount", "I").i == before + 1, "startPreview resumes frame delivery");

    CallVirtual(camera, "release", "()V", {}, nullptr);
    Check(g_host.closed >= 1, "release closes the host session");
    PushFrame(0);
    Dispatch();
    Check(Static("frameCount", "I").i == before + 1, "a released camera receives nothing");
}

}  // namespace

int main() {
    std::printf("=== KuART camera ===\n");

    RecorderSpecs probe;
    DexBuilder index_builder;
    index_builder.Build(RecorderClasses(probe));
    const char* cls = "Lcom/foo/CamRecorder;";
    RecorderSpecs specs;
    specs.ctor.code = {Op11x(kOpReturnVoid, 0)};
    specs.ctor.registers_size = 1;
    specs.ctor.ins_size = 1;
    FillRecorder(&specs.on_frame,
                 static_cast<uint16_t>(index_builder.FieldIndexOf(cls, probe.last_frame)),
                 static_cast<uint16_t>(index_builder.FieldIndexOf(cls, probe.frame_count)));
    FillRecorder(&specs.on_photo,
                 static_cast<uint16_t>(index_builder.FieldIndexOf(cls, probe.last_photo)),
                 static_cast<uint16_t>(index_builder.FieldIndexOf(cls, probe.photo_count)));
    DexBuilder builder;
    const std::vector<uint8_t> test_dex = builder.Build(RecorderClasses(specs));

    DexClassLinker linker;
    std::string error;
    if (!linker.AddDexFile(g_framework_dex_bytes, g_framework_dex_size, "framework.dex", &error)) {
        std::printf("  FAIL AddDexFile(framework.dex): %s\n", error.c_str());
        return 1;
    }
    if (!linker.AddDexFile(test_dex.data(), test_dex.size(), "test.dex", &error)) {
        std::printf("  FAIL AddDexFile(test.dex): %s\n", error.c_str());
        return 1;
    }
    Interpreter interp(&linker);
    DexJniEnv jni(&linker, &interp);
    interp.set_jni_env(&jni);
    interp.set_instruction_limit(2000ull * 1000ull * 1000ull);
    g_linker = &linker;
    g_interp = &interp;

    DexObject* recorder = NewObject(cls, "()V", {});
    Check(recorder != nullptr, "the recording callback class is available");

    kudroid_set_camera_host(&kHost);
    test_native_frame_contract();
    test_camera_api(recorder);
    kudroid_set_camera_host(nullptr);

    std::printf("=== %d checks, %d failures ===\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
