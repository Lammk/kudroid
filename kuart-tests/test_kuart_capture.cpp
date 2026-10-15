// test_kuart_capture.cpp - runtime permissions and microphone capture, driven through the
// real framework.dex: Context permission checks, the request/response bridge, and
// android.media.AudioRecord reading PCM the host feeds in.
#include "kudroid/framework_dex_bytes.h"
#include "kudroid/kudroid_bridge.h"
#include "kudroid/kuart/DexClassLinker.h"
#include "kudroid/kuart/DexJniEnv.h"
#include "kudroid/kuart/DexObject.h"
#include "kudroid/kuart/DexString.h"
#include "kudroid/kuart/Interpreter.h"
#include "kudroid/platform/AudioCapture.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {

using kudroid::kuart::DexArray;
using kudroid::kuart::DexClass;
using kudroid::kuart::DexClassLinker;
using kudroid::kuart::DexJniEnv;
using kudroid::kuart::DexMethod;
using kudroid::kuart::DexObject;
using kudroid::kuart::DexString;
using kudroid::kuart::DexValue;
using kudroid::kuart::Interpreter;

int g_checks = 0;
int g_failures = 0;
DexClassLinker* g_linker = nullptr;
Interpreter* g_interp = nullptr;

constexpr const char* kPkg = "com.example.capture";
constexpr const char* kMic = "android.permission.RECORD_AUDIO";
constexpr const char* kCamera = "android.permission.CAMERA";

constexpr int kChannelInMono = 16;
constexpr int kEncodingPcm16 = 2;
constexpr int kReadNonBlocking = 1;
constexpr int kPermissionGranted = 0;
constexpr int kPermissionDenied = -1;

void Check(bool condition, const char* message) {
    ++g_checks;
    std::printf("%s %s\n", condition ? "  OK  " : "  FAIL", message);
    if (!condition) ++g_failures;
}

DexValue Str(const char* value) {
    return DexValue::Ref(reinterpret_cast<DexObject*>(g_linker->NewString(value)));
}

// Runs a method; reports whether it threw instead of failing, so tests can expect throws.
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

DexObject* NewObject(const char* descriptor, const char* sig, std::vector<DexValue> args) {
    DexClass* klass = g_linker->FindClass(descriptor);
    if (klass == nullptr || klass->is_stub || !g_interp->EnsureInitialized(klass)) return nullptr;
    DexObject* object = g_linker->AllocObject(klass);
    DexMethod* ctor = klass->FindDirectMethod("<init>", sig);
    if (object == nullptr || ctor == nullptr) return nullptr;
    args.insert(args.begin(), DexValue::Ref(object));
    return Run(ctor, args, nullptr, nullptr) ? object : nullptr;
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

int CheckPermission(DexObject* context, const char* permission) {
    DexValue v;
    if (!CallVirtual(context, "checkSelfPermission", "(Ljava/lang/String;)I", {Str(permission)}, &v)) {
        return 99;
    }
    return v.i;
}

DexArray* NewArray(const char* descriptor, int count) {
    DexClass* klass = g_linker->FindClass(descriptor);
    return klass != nullptr ? g_linker->AllocArray(klass, count) : nullptr;
}

int g_promptCalls = 0;
void PromptCallback(const char*, const char*, int, void*) { ++g_promptCalls; }

void test_permissions(DexObject* context) {
    std::printf("[permissions] microphone and camera follow the app grant and the host\n");
    kudroid_set_host_authorization(0, 0);
    kudroid_set_host_authorization(1, 0);
    Check(CheckPermission(context, kMic) == kPermissionDenied,
          "RECORD_AUDIO is denied before anything is granted");
    Check(CheckPermission(context, kCamera) == kPermissionDenied,
          "CAMERA is denied before anything is granted");
    Check(CheckPermission(context, "android.permission.INTERNET") == kPermissionGranted,
          "permissions that need no host authorization stay granted");

    kudroid_set_host_authorization(0, 1);
    Check(CheckPermission(context, kMic) == kPermissionDenied,
          "host authorization alone does not grant the app");

    kudroid_set_permission_prompt_callback(PromptCallback);
    g_promptCalls = 0;
    kudroid_prompt_permission_request(kPkg, kMic, 11, nullptr);
    Check(g_promptCalls == 1, "a missing grant raises the host prompt");

    kudroid_submit_permission_response(nullptr, 11, kMic, 1);
    Check(CheckPermission(context, kMic) == kPermissionGranted,
          "allowing in the prompt grants RECORD_AUDIO once the host agrees");
    g_promptCalls = 0;
    kudroid_prompt_permission_request(kPkg, kMic, 12, nullptr);
    Check(g_promptCalls == 0, "a request for permissions already held shows no prompt");

    kudroid_submit_permission_response(nullptr, 13, kCamera, 1);
    Check(CheckPermission(context, kCamera) == kPermissionDenied,
          "an app grant without host authorization is still denied");
    kudroid_set_host_authorization(1, 1);
    Check(CheckPermission(context, kCamera) == kPermissionGranted,
          "CAMERA is granted when both the app and the host allow it");

    kudroid_submit_permission_response(nullptr, 14, kMic, 0);
    Check(CheckPermission(context, kMic) == kPermissionDenied, "denying revokes the grant");
    kudroid_set_permission_prompt_callback(nullptr);
}

// Feed `frames` mono samples (a ramp starting at `first`) as the capture device would.
void Feed(int first, int frames) {
    std::vector<int16_t> pcm(frames);
    for (int i = 0; i < frames; ++i) pcm[i] = static_cast<int16_t>(first + i);
    kudroid_audio_capture_test_feed(pcm.data(), frames);
}

void test_audiorecord(DexObject* context) {
    std::printf("[audiorecord] real capture through the framework class\n");
    (void)context;
    DexValue v;
    Check(CallStatic("Landroid/media/AudioRecord;", "getMinBufferSize", "(III)I",
                     {DexValue::Int(16000), DexValue::Int(kChannelInMono), DexValue::Int(kEncodingPcm16)},
                     &v) && v.i == 1280,
          "minimum buffer is 40 ms of 16-bit mono");
    Check(CallStatic("Landroid/media/AudioRecord;", "getMinBufferSize", "(III)I",
                     {DexValue::Int(16000), DexValue::Int(kChannelInMono), DexValue::Int(99)}, &v) &&
              v.i == -2,
          "an unsupported encoding is a bad value");
    Check(CallStatic("Landroid/media/AudioRecord;", "getMinBufferSize", "(III)I",
                     {DexValue::Int(100), DexValue::Int(kChannelInMono), DexValue::Int(kEncodingPcm16)},
                     &v) && v.i == -2,
          "an implausible sample rate is a bad value");

    // Denied by the host: the recorder must not claim to be initialized.
    kudroid_set_host_authorization(0, 2);
    DexObject* denied = NewObject("Landroid/media/AudioRecord;", "(IIIII)V",
            {DexValue::Int(1), DexValue::Int(16000), DexValue::Int(kChannelInMono),
             DexValue::Int(kEncodingPcm16), DexValue::Int(0)});
    CallVirtual(denied, "getState", "()I", {}, &v);
    Check(denied != nullptr && v.i == 0, "an unauthorized microphone leaves the recorder uninitialized");
    bool threw = false;
    CallVirtual(denied, "startRecording", "()V", {}, nullptr, &threw);
    Check(threw, "startRecording on an uninitialized recorder throws IllegalStateException");

    // Authorized and granted.
    // AudioRecord has no Context: it reads the package from ActivityThread, which is
    // empty on this host, so the grant goes under the empty package.
    kudroid_set_host_authorization(0, 1);
    kudroid_prompt_permission_request("", kMic, 20, nullptr);
    DexObject* rec = NewObject("Landroid/media/AudioRecord;", "(IIIII)V",
            {DexValue::Int(1), DexValue::Int(16000), DexValue::Int(kChannelInMono),
             DexValue::Int(kEncodingPcm16), DexValue::Int(0)});
    CallVirtual(rec, "getState", "()I", {}, &v);
    Check(rec != nullptr && v.i == 1, "an authorized, granted recorder is initialized");
    Check(kudroid_audio_capture_active() == 1, "the host sees one open recorder");

    DexArray* shorts = NewArray("[S", 512);
    CallVirtual(rec, "startRecording", "()V", {}, nullptr);
    CallVirtual(rec, "getRecordingState", "()I", {}, &v);
    Check(v.i == 3, "startRecording moves to RECORDSTATE_RECORDING");

    CallVirtual(rec, "read", "([SIII)I",
                {DexValue::Ref(reinterpret_cast<DexObject*>(shorts)), DexValue::Int(0),
                 DexValue::Int(64), DexValue::Int(kReadNonBlocking)}, &v);
    Check(v.i == 0, "a non-blocking read with nothing captured returns 0");

    // A blocking read waits for the capture device to deliver the whole request.
    std::thread feeder([] {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        Feed(100, 160);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        Feed(260, 160);
    });
    CallVirtual(rec, "read", "([SII)I",
                {DexValue::Ref(reinterpret_cast<DexObject*>(shorts)), DexValue::Int(0),
                 DexValue::Int(320)}, &v);
    feeder.join();
    bool ramp = v.i == 320;
    for (int i = 0; ramp && i < 320; ++i) ramp = shorts->Get<int16_t>(i) == 100 + i;
    Check(ramp, "a blocking read returns the full request, in capture order");

    DexArray* bytes = NewArray("[B", 256);
    Feed(1, 100);
    CallVirtual(rec, "read", "([BIII)I",
                {DexValue::Ref(reinterpret_cast<DexObject*>(bytes)), DexValue::Int(0),
                 DexValue::Int(200), DexValue::Int(kReadNonBlocking)}, &v);
    Check(v.i == 200 && bytes->Get<int8_t>(0) == 1 && bytes->Get<int8_t>(1) == 0 &&
              bytes->Get<int8_t>(198) == 100,
          "byte reads carry little-endian 16-bit samples");

    // Overrun keeps the newest audio.
    for (int i = 0; i < 20; ++i) Feed(i * 1000, 1000);
    CallVirtual(rec, "read", "([SIII)I",
                {DexValue::Ref(reinterpret_cast<DexObject*>(shorts)), DexValue::Int(0),
                 DexValue::Int(1), DexValue::Int(kReadNonBlocking)}, &v);
    Check(v.i == 1 && shorts->Get<int16_t>(0) != 0,
          "when the reader falls behind, the oldest samples are dropped");

    CallVirtual(rec, "stop", "()V", {}, nullptr);
    // Audio captured before stop() can still be drained; once it is gone, reads are invalid.
    int drained = 0;
    v.i = 0;
    for (int i = 0; i < 64 && v.i >= 0; ++i) {
        CallVirtual(rec, "read", "([SIII)I",
                    {DexValue::Ref(reinterpret_cast<DexObject*>(shorts)), DexValue::Int(0),
                     DexValue::Int(512), DexValue::Int(kReadNonBlocking)}, &v);
        if (v.i > 0) drained += v.i;
    }
    Check(drained > 0 && v.i == -3,
          "a stopped recorder drains what it captured, then reads are an invalid operation");

    CallVirtual(rec, "startRecording", "()V", {}, nullptr);
    Feed(7, 50);
    CallVirtual(rec, "read", "([SIII)I",
                {DexValue::Ref(reinterpret_cast<DexObject*>(shorts)), DexValue::Int(0),
                 DexValue::Int(50), DexValue::Int(kReadNonBlocking)}, &v);
    Check(v.i == 50 && shorts->Get<int16_t>(0) == 7, "a recorder can be restarted after stop");

    CallVirtual(rec, "release", "()V", {}, nullptr);
    CallVirtual(rec, "getState", "()I", {}, &v);
    Check(v.i == 0 && kudroid_audio_capture_active() == 0,
          "release uninitializes the recorder and closes the host input");
}

}  // namespace

int main() {
    std::printf("=== KuART runtime permissions and microphone capture ===\n");
    DexClassLinker linker;
    std::string error;
    if (!linker.AddDexFile(g_framework_dex_bytes, g_framework_dex_size, "framework.dex", &error)) {
        std::printf("  FAIL AddDexFile(framework.dex): %s\n", error.c_str());
        return 1;
    }
    Interpreter interp(&linker);
    DexJniEnv jni(&linker, &interp);
    interp.set_jni_env(&jni);
    interp.set_instruction_limit(2000ull * 1000ull * 1000ull);
    g_linker = &linker;
    g_interp = &interp;

    DexObject* context = NewObject("Landroid/app/ApplicationContext;", "(Ljava/lang/String;)V",
                                   {Str(kPkg)});
    Check(context != nullptr, "an application context is available");
    test_permissions(context);
    test_audiorecord(context);

    std::printf("=== %d checks, %d failures ===\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
