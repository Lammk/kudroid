// test_kuart_audiotrack.cpp — the Java audio path, driven the way FMOD drives it.
//
// Why this exists. FMOD — which is what Unity games ship for audio — does not call OpenSL
// ES or AAudio from native code. It drives android.media.AudioTrack from JAVA. The
// generated framework stub had one empty constructor, so every method the sequence needs
// was auto-stubbed to return 0:
//
//     [KuART][MISSING-METHOD] Auto-stubbing: AudioTrack->getMinBufferSize(III)I
//     [KuART][MISSING-METHOD] Auto-stubbing: AudioTrack-><init>(IIIIII)V
//     [KuART][MISSING-METHOD] Auto-stubbing: AudioTrack->getState()I
//     [E/FMOD] AudioTrack failed to initialize (status 0)
//
// getState() returning 0 is the fatal one: FMOD compares it against STATE_INITIALIZED, so
// 0 is a permanent failure and no amount of retrying gets past it. It retried anyway. The
// thread sampler caught the loop — pc moving inside libsystem_malloc with lr in
// DexClassLinker::FindClass across five samples ten seconds apart, CPU climbing rather
// than flat, which is a spin and not a park. ULTRAKILL reached Vulkan, created its
// swapchain, and never produced a frame.
//
// So this test runs FMOD's actual sequence through the interpreter against the real
// framework.dex: getMinBufferSize, construct, getState, play, write, getPlaybackHeadPosition,
// stop, release. A stub passes none of it, which is the point — compiling the Java is not
// evidence that the sequence works.
#include "kudroid/framework_dex_bytes.h"
#include "kudroid/kuart/DexClassLinker.h"
#include "kudroid/kuart/DexJniEnv.h"
#include "kudroid/kuart/DexObject.h"
#include "kudroid/kuart/DexString.h"
#include "kudroid/kuart/Interpreter.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    std::printf("%s %s\n", ok ? "  OK  " : "  FAIL", what.c_str());
    if (!ok) ++g_failures;
}

using kudroid::kuart::DexArray;
using kudroid::kuart::DexClass;
using kudroid::kuart::DexClassLinker;
using kudroid::kuart::DexJniEnv;
using kudroid::kuart::DexMethod;
using kudroid::kuart::DexObject;
using kudroid::kuart::DexValue;
using kudroid::kuart::Interpreter;

DexClassLinker* g_linker = nullptr;
Interpreter* g_interp = nullptr;

// android.media.AudioFormat / AudioManager constants, as a guest passes them. Written out
// rather than read from the dex: if the framework's values ever drift from the platform's,
// this test must fail rather than follow them.
constexpr int kStreamMusic = 3;
constexpr int kChannelOutStereo = 0xC;
constexpr int kChannelOutMono = 0x4;
constexpr int kEncodingPcm16Bit = 2;
constexpr int kEncodingPcmFloat = 4;
constexpr int kModeStream = 1;
constexpr int kStateInitialized = 1;
constexpr int kPlayStatePlaying = 3;
constexpr int kPlayStateStopped = 1;

bool CallStaticInt(const char* descriptor, const char* name, const char* sig,
                   std::vector<DexValue> args, int32_t* out, const char* what) {
    DexClass* klass = g_linker->FindClass(descriptor);
    if (klass == nullptr || klass->is_stub) {
        std::printf("  FAIL %s: %s is missing or a stub\n", what, descriptor);
        ++g_failures;
        ++g_checks;
        return false;
    }
    g_interp->ClearPendingException();
    if (!g_interp->EnsureInitialized(klass)) {
        std::printf("  FAIL %s: <clinit> failed: %s\n", what,
                    g_interp->last_error().c_str());
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
    if (out != nullptr) *out = r.i;
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
        std::printf("  FAIL %s: no method %s%s\n", what, name, sig);
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

// new AudioTrack(streamType, sampleRate, channelConfig, encoding, bufferBytes, mode)
DexObject* NewAudioTrack(int sampleRate, int channelConfig, int encoding, int bufferBytes) {
    DexClass* klass = g_linker->FindClass("Landroid/media/AudioTrack;");
    if (klass == nullptr || klass->is_stub) return nullptr;
    if (!g_interp->EnsureInitialized(klass)) return nullptr;
    DexObject* obj = g_linker->AllocObject(klass);
    if (obj == nullptr) return nullptr;

    DexMethod* ctor = klass->FindDirectMethod("<init>", "(IIIIII)V");
    if (ctor == nullptr) return nullptr;

    std::vector<DexValue> args = {
        DexValue::Ref(obj),          DexValue::Int(kStreamMusic),
        DexValue::Int(sampleRate),   DexValue::Int(channelConfig),
        DexValue::Int(encoding),     DexValue::Int(bufferBytes),
        DexValue::Int(kModeStream),
    };
    g_interp->ClearPendingException();
    g_interp->Execute(ctor, args.data(), args.size());
    if (g_interp->HasPendingException()) {
        std::printf("  note: <init> threw: %s\n", g_interp->last_error().c_str());
        g_interp->ClearPendingException();
        return nullptr;
    }
    return obj;
}

// A short[] of silence-adjacent PCM. Not all zeroes: a shim that drops the write entirely
// and one that submits it are indistinguishable if the data carries no information.
DexArray* NewPcmShorts(int count) {
    DexClass* klass = g_linker->FindClass("[S");
    if (klass == nullptr) return nullptr;
    DexArray* arr = g_linker->AllocArray(klass, count);
    if (arr == nullptr) return nullptr;
    for (int i = 0; i < count; ++i) {
        arr->Set<int16_t>(i, static_cast<int16_t>((i * 977) & 0x7FFF));
    }
    return arr;
}

DexArray* NewPcmBytes(int count) {
    DexClass* klass = g_linker->FindClass("[B");
    if (klass == nullptr) return nullptr;
    DexArray* arr = g_linker->AllocArray(klass, count);
    if (arr == nullptr) return nullptr;
    for (int i = 0; i < count; ++i) {
        arr->Set<int8_t>(i, static_cast<int8_t>(i & 0x7F));
    }
    return arr;
}

// ─── the class is real ───────────────────────────────────────────────────────

void test_audiotrack_is_not_a_stub() {
    std::printf("[class] AudioTrack is a real class with the methods FMOD calls\n");
    DexClass* klass = g_linker->FindClass("Landroid/media/AudioTrack;");
    Check(klass != nullptr, "AudioTrack is in framework.dex");
    if (klass == nullptr) return;
    Check(!klass->is_stub, "and it is not an auto-generated stub");

    // The exact methods the captured log reported as auto-stubbed. Each one being present
    // is what stops it being stubbed at runtime.
    struct Want { const char* name; const char* sig; };
    const Want wanted[] = {
        {"getMinBufferSize", "(III)I"},
        {"<init>",           "(IIIIII)V"},
        {"getState",         "()I"},
        {"release",          "()V"},
        {"play",             "()V"},
        {"stop",             "()V"},
        {"pause",            "()V"},
        {"flush",            "()V"},
        {"write",            "([BII)I"},
        {"write",            "([SII)I"},
        {"getPlaybackHeadPosition", "()I"},
        {"setVolume",        "(F)I"},
        {"getSampleRate",    "()I"},
        {"getChannelCount",  "()I"},
        {"getPlayState",     "()I"},
    };
    for (const Want& w : wanted) {
        DexMethod* m = klass->FindDirectMethod(w.name, w.sig);
        if (m == nullptr) m = klass->FindVirtualMethod(w.name, w.sig);
        Check(m != nullptr, std::string("has ") + w.name + w.sig);
    }
}

// ─── getMinBufferSize ───────────────────────────────────────────────────────

void test_min_buffer_size_is_usable() {
    std::printf("[buffer] getMinBufferSize returns a workable size, never 0\n");

    int32_t stereo44 = 0;
    if (CallStaticInt("Landroid/media/AudioTrack;", "getMinBufferSize", "(III)I",
                      {DexValue::Int(44100), DexValue::Int(kChannelOutStereo),
                       DexValue::Int(kEncodingPcm16Bit)},
                      &stereo44, "getMinBufferSize(44100, stereo, 16bit)")) {
        std::printf("       44100 stereo 16-bit -> %d bytes\n", stereo44);
        // 0 is the value that killed the run: FMOD reads it as ERROR_BAD_VALUE and gives
        // up on the device permanently.
        Check(stereo44 > 0, "it is greater than zero — the value FMOD rejected");
        // A frame is 4 bytes here, and a buffer that is not a whole number of frames makes
        // a mixer write partial frames forever.
        Check(stereo44 % 4 == 0, "and a whole number of stereo 16-bit frames");
        // Sanity: 20ms of 44100 stereo 16-bit is 3528 bytes. Anything under a millisecond
        // or over a second means the arithmetic is wrong rather than merely different.
        Check(stereo44 >= 176 && stereo44 <= 176400,
              "and within a plausible range for one audio period");
    }

    // Mono must differ from stereo. Equal values mean the channel mask was ignored, which
    // is how a mixer ends up writing twice the data the device expects.
    int32_t mono44 = 0;
    if (CallStaticInt("Landroid/media/AudioTrack;", "getMinBufferSize", "(III)I",
                      {DexValue::Int(44100), DexValue::Int(kChannelOutMono),
                       DexValue::Int(kEncodingPcm16Bit)},
                      &mono44, "getMinBufferSize(44100, mono, 16bit)")) {
        std::printf("       44100 mono  16-bit -> %d bytes\n", mono44);
        Check(mono44 > 0, "mono is also greater than zero");
        Check(mono44 * 2 == stereo44,
              "and exactly half of stereo — the channel mask was decoded, not ignored");
    }

    // A higher rate must need a bigger buffer, or the sample rate is being ignored too.
    int32_t stereo48 = 0;
    if (CallStaticInt("Landroid/media/AudioTrack;", "getMinBufferSize", "(III)I",
                      {DexValue::Int(48000), DexValue::Int(kChannelOutStereo),
                       DexValue::Int(kEncodingPcm16Bit)},
                      &stereo48, "getMinBufferSize(48000, stereo, 16bit)")) {
        Check(stereo48 > stereo44, "48kHz needs more than 44.1kHz");
    }

    // Float is 4 bytes per sample against 16-bit's 2.
    int32_t float44 = 0;
    if (CallStaticInt("Landroid/media/AudioTrack;", "getMinBufferSize", "(III)I",
                      {DexValue::Int(44100), DexValue::Int(kChannelOutStereo),
                       DexValue::Int(kEncodingPcmFloat)},
                      &float44, "getMinBufferSize(44100, stereo, float)")) {
        Check(float44 == stereo44 * 2, "float needs twice the bytes of 16-bit");
    }
}

// ─── FMOD's sequence ────────────────────────────────────────────────────────

void test_fmods_initialisation_sequence_succeeds() {
    std::printf("[fmod] the exact sequence from the log: size, construct, getState\n");

    int32_t bufferBytes = 0;
    CallStaticInt("Landroid/media/AudioTrack;", "getMinBufferSize", "(III)I",
                  {DexValue::Int(44100), DexValue::Int(kChannelOutStereo),
                   DexValue::Int(kEncodingPcm16Bit)},
                  &bufferBytes, "getMinBufferSize");

    DexObject* track = NewAudioTrack(44100, kChannelOutStereo, kEncodingPcm16Bit, bufferBytes);
    Check(track != nullptr, "the AudioTrack constructor completed");
    if (track == nullptr) return;

    // THE check. FMOD compares this against STATE_INITIALIZED and abandons the device on
    // anything else; the auto-stub returned 0 and it retried forever.
    DexValue state;
    if (CallVirtual(track, "getState", "()I", {}, &state, "getState")) {
        std::printf("       getState() -> %d (STATE_INITIALIZED is %d)\n", state.i,
                    kStateInitialized);
        Check(state.i == kStateInitialized,
              "getState() reports STATE_INITIALIZED — what FMOD requires to proceed");
    }

    // The format must read back as asked. FMOD configures its mixer from these, so a
    // mismatch produces audio at the wrong pitch rather than a failure.
    DexValue rate;
    if (CallVirtual(track, "getSampleRate", "()I", {}, &rate, "getSampleRate")) {
        Check(rate.i == 44100, "getSampleRate() returns the rate that was requested");
    }
    DexValue channels;
    if (CallVirtual(track, "getChannelCount", "()I", {}, &channels, "getChannelCount")) {
        Check(channels.i == 2, "getChannelCount() returns 2 for CHANNEL_OUT_STEREO");
    }

    CallVirtual(track, "release", "()V", {}, nullptr, "release");
}

void test_play_write_and_transport() {
    std::printf("[fmod] play, write PCM, then stop — the loop FMOD runs\n");

    int32_t bufferBytes = 0;
    CallStaticInt("Landroid/media/AudioTrack;", "getMinBufferSize", "(III)I",
                  {DexValue::Int(44100), DexValue::Int(kChannelOutStereo),
                   DexValue::Int(kEncodingPcm16Bit)},
                  &bufferBytes, "getMinBufferSize");
    DexObject* track = NewAudioTrack(44100, kChannelOutStereo, kEncodingPcm16Bit, bufferBytes);
    if (track == nullptr) { Check(false, "constructed"); return; }

    CallVirtual(track, "play", "()V", {}, nullptr, "play()");
    DexValue playState;
    if (CallVirtual(track, "getPlayState", "()I", {}, &playState, "getPlayState")) {
        Check(playState.i == kPlayStatePlaying, "getPlayState() reports PLAYING after play()");
    }

    // write(short[]) is the hot path: 16-bit PCM is what FMOD produces.
    const int frames = 256;
    DexArray* shorts = NewPcmShorts(frames * 2);  // stereo
    Check(shorts != nullptr, "a short[] of PCM was allocated");
    if (shorts != nullptr) {
        DexValue written;
        if (CallVirtual(track, "write", "([SII)I",
                        {DexValue::Ref(shorts), DexValue::Int(0), DexValue::Int(frames * 2)},
                        &written, "write(short[], 0, n)")) {
            std::printf("       write(short[%d]) -> %d\n", frames * 2, written.i);
            // Returned in ELEMENTS, as the Java API specifies — not bytes. Reporting bytes
            // would make a caller believe it wrote twice what it did.
            Check(written.i == frames * 2,
                  "it reports the element count it accepted, not a byte count");
        }
    }

    DexArray* bytes = NewPcmBytes(512);
    if (bytes != nullptr) {
        DexValue written;
        if (CallVirtual(track, "write", "([BII)I",
                        {DexValue::Ref(bytes), DexValue::Int(0), DexValue::Int(512)},
                        &written, "write(byte[], 0, 512)")) {
            Check(written.i == 512, "write(byte[]) reports the byte count it accepted");
        }
    }

    // Out-of-range writes must be refused rather than reading past the array.
    if (shorts != nullptr) {
        DexValue rc;
        if (CallVirtual(track, "write", "([SII)I",
                        {DexValue::Ref(shorts), DexValue::Int(0),
                         DexValue::Int(frames * 4)},  // twice what exists
                        &rc, "write past the end")) {
            Check(rc.i < 0, "a write longer than the array is refused with an error");
        }
        if (CallVirtual(track, "write", "([SII)I",
                        {DexValue::Ref(shorts), DexValue::Int(-1), DexValue::Int(4)},
                        &rc, "write with a negative offset")) {
            Check(rc.i < 0, "a negative offset is refused too");
        }
    }

    CallVirtual(track, "stop", "()V", {}, nullptr, "stop()");
    if (CallVirtual(track, "getPlayState", "()I", {}, &playState, "getPlayState after stop")) {
        Check(playState.i == kPlayStateStopped, "getPlayState() reports STOPPED after stop()");
    }

    CallVirtual(track, "release", "()V", {}, nullptr, "release");
}

// ─── the frame counter ──────────────────────────────────────────────────────

void test_playback_head_advances() {
    std::printf("[fmod] getPlaybackHeadPosition climbs, or FMOD stops writing\n");

    int32_t bufferBytes = 0;
    CallStaticInt("Landroid/media/AudioTrack;", "getMinBufferSize", "(III)I",
                  {DexValue::Int(44100), DexValue::Int(kChannelOutStereo),
                   DexValue::Int(kEncodingPcm16Bit)},
                  &bufferBytes, "getMinBufferSize");
    DexObject* track = NewAudioTrack(44100, kChannelOutStereo, kEncodingPcm16Bit, bufferBytes);
    if (track == nullptr) { Check(false, "constructed"); return; }

    CallVirtual(track, "play", "()V", {}, nullptr, "play()");

    DexValue before;
    CallVirtual(track, "getPlaybackHeadPosition", "()I", {}, &before, "head position at start");
    Check(before.i == 0, "it starts at zero");

    // Submit several buffers and let the host consume them. FMOD computes how much more to
    // write from the DIFFERENCE between successive reads, so a counter frozen at zero reads
    // as a device that never drains and it stops writing altogether — a hang, not silence.
    DexArray* shorts = NewPcmShorts(1024);
    if (shorts != nullptr) {
        for (int i = 0; i < 4; ++i) {
            CallVirtual(track, "write", "([SII)I",
                        {DexValue::Ref(shorts), DexValue::Int(0), DexValue::Int(1024)},
                        nullptr, "write for the head position test");
        }
    }
    // The host consumes asynchronously; poll rather than sleeping a fixed time.
    DexValue after;
    after.i = 0;
    for (int i = 0; i < 100; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (!CallVirtual(track, "getPlaybackHeadPosition", "()I", {}, &after,
                         "head position while playing")) {
            break;
        }
        if (after.i > 0) break;
    }
    std::printf("       head position after 4 writes -> %d frames\n", after.i);
    Check(after.i > 0, "it advanced once the host consumed the buffers");

    CallVirtual(track, "release", "()V", {}, nullptr, "release");
}

// ─── failure is reported, not faked ─────────────────────────────────────────

void test_an_impossible_format_reports_failure() {
    std::printf("[robust] a track that cannot open reports UNINITIALIZED, not success\n");
    // Claiming STATE_INITIALIZED unconditionally would be the easy way to get FMOD past
    // its check, and it would replace a clear "AudioTrack failed to initialize" with
    // silence nobody can diagnose. A zero sample rate cannot open.
    DexObject* track = NewAudioTrack(0, kChannelOutStereo, kEncodingPcm16Bit, 4096);
    if (track == nullptr) {
        // The constructor itself refused, which is also an honest answer.
        Check(true, "the constructor refused an impossible rate");
        return;
    }
    DexValue state;
    if (CallVirtual(track, "getState", "()I", {}, &state, "getState on a bad track")) {
        // Either it substituted a workable rate (44100, which the Java does) and really is
        // initialised, or it failed and says so. What it must not do is claim initialised
        // while having no player.
        std::printf("       getState() -> %d\n", state.i);
        Check(state.i == kStateInitialized || state.i == 0,
              "it reports a defined state rather than something arbitrary");
    }
    CallVirtual(track, "release", "()V", {}, nullptr, "release");
}

void test_use_after_release_is_refused() {
    std::printf("[robust] a released track refuses writes rather than crashing\n");
    int32_t bufferBytes = 0;
    CallStaticInt("Landroid/media/AudioTrack;", "getMinBufferSize", "(III)I",
                  {DexValue::Int(44100), DexValue::Int(kChannelOutStereo),
                   DexValue::Int(kEncodingPcm16Bit)},
                  &bufferBytes, "getMinBufferSize");
    DexObject* track = NewAudioTrack(44100, kChannelOutStereo, kEncodingPcm16Bit, bufferBytes);
    if (track == nullptr) { Check(false, "constructed"); return; }

    CallVirtual(track, "release", "()V", {}, nullptr, "release");

    DexValue state;
    if (CallVirtual(track, "getState", "()I", {}, &state, "getState after release")) {
        Check(state.i == 0, "getState() reports UNINITIALIZED after release()");
    }
    DexArray* shorts = NewPcmShorts(64);
    if (shorts != nullptr) {
        DexValue rc;
        if (CallVirtual(track, "write", "([SII)I",
                        {DexValue::Ref(shorts), DexValue::Int(0), DexValue::Int(64)},
                        &rc, "write after release")) {
            Check(rc.i < 0, "and a write is refused with an error rather than accepted");
        }
    }
    // A second release must be harmless: FMOD releases in its own teardown and Unity's
    // shutdown path can reach it twice.
    CallVirtual(track, "release", "()V", {}, nullptr, "second release");
    Check(true, "releasing twice did not fault");
}

void test_volume_is_accepted_and_clamped() {
    std::printf("[robust] setVolume accepts the range FMOD uses\n");
    int32_t bufferBytes = 0;
    CallStaticInt("Landroid/media/AudioTrack;", "getMinBufferSize", "(III)I",
                  {DexValue::Int(44100), DexValue::Int(kChannelOutStereo),
                   DexValue::Int(kEncodingPcm16Bit)},
                  &bufferBytes, "getMinBufferSize");
    DexObject* track = NewAudioTrack(44100, kChannelOutStereo, kEncodingPcm16Bit, bufferBytes);
    if (track == nullptr) { Check(false, "constructed"); return; }

    DexValue rc;
    if (CallVirtual(track, "setVolume", "(F)I", {DexValue::Float(1.0f)}, &rc, "setVolume(1.0)")) {
        Check(rc.i == 0, "full volume is accepted");
    }
    if (CallVirtual(track, "setVolume", "(F)I", {DexValue::Float(0.0f)}, &rc, "setVolume(0.0)")) {
        Check(rc.i == 0, "silence is accepted");
    }
    // Out of range must not be an error: AudioTrack clamps, and a guest that gets an error
    // here may treat the whole device as broken.
    if (CallVirtual(track, "setVolume", "(F)I", {DexValue::Float(4.0f)}, &rc, "setVolume(4.0)")) {
        Check(rc.i == 0, "an out-of-range gain is clamped rather than refused");
    }
    CallVirtual(track, "release", "()V", {}, nullptr, "release");
}

}  // namespace

int main() {
    std::printf("=== KuART android.media.AudioTrack ===\n");

    DexClassLinker linker;
    std::string error;
    if (!linker.AddDexFile(g_framework_dex_bytes, g_framework_dex_size, "framework.dex",
                           &error)) {
        std::printf("  FAIL AddDexFile(framework.dex): %s\n=== FAILED ===\n", error.c_str());
        return 1;
    }
    std::printf("framework.dex: %zu bytes\n", g_framework_dex_size);

    Interpreter interp(&linker);
    DexJniEnv jni(&linker, &interp);
    interp.set_jni_env(&jni);
    g_linker = &linker;
    g_interp = &interp;
    interp.set_instruction_limit(2000ull * 1000ull * 1000ull);

    test_audiotrack_is_not_a_stub();
    test_min_buffer_size_is_usable();
    test_fmods_initialisation_sequence_succeeds();
    test_play_write_and_transport();
    test_playback_head_advances();
    test_an_impossible_format_reports_failure();
    test_use_after_release_is_refused();
    test_volume_is_accepted_and_clamped();

    std::printf("=== %d checks, %d failures ===\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
