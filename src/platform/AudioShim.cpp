#include "kudroid/abi/SyscallShim.h"
#include "kudroid/platform/AudioShim.h"

#include <cstdint>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <vector>
#include <deque>
#include <limits>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <chrono>
#include <unordered_map>
#include <memory>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <string>

// Log directory owned by the bridge; weak so the shim also links on its own.
extern const char* g_kudroid_log_dir_ptr __attribute__((weak));

#if defined(__APPLE__)
#include <AudioToolbox/AudioToolbox.h>
#include <objc/runtime.h>
#include <objc/message.h>
#endif

// ─────────────────────────────────────────────────────────────────────────────
// AudioShim — Unified CoreAudio Ring Buffer Output.
//
// Shared pipeline for OpenSL ES, AAudio, and android.media.AudioTrack.
// Fixed pre-allocated AudioQueue buffers on iOS with zero dynamic allocation
// in playback loop. Paced by CoreAudio hardware consumption via Ring Buffer.
// ─────────────────────────────────────────────────────────────────────────────

namespace kudroid {
namespace {

typedef int32_t SLresult;
#define SL_RESULT_SUCCESS 0
#define SL_RESULT_PARAMETER_INVALID 2
#define SL_RESULT_RESOURCE_ERROR 4
#define SL_RESULT_BUFFER_INSUFFICIENT 8
#define SL_RESULT_CONTENT_UNSUPPORTED 9

typedef void* SLObjectItf;
typedef void* SLEngineItf;
typedef void* SLAndroidSimpleBufferQueueItf;
typedef void* SLPlayItf;
typedef void* SLVolumeItf;
typedef void* SLInterfaceID;
typedef void* SLboolean;

struct SLDataFormat_PCM {
    uint32_t formatType;      // SL_DATAFORMAT_PCM = 3
    uint32_t numChannels;
    uint32_t samplesPerSec;   // milliHz in OpenSL ES
    uint32_t bitsPerSample;
    uint32_t containerSize;
    uint32_t channelMask;
    uint32_t endianness;
};
struct SLDataSource {
    const void* pLocator;
    const void* pFormat;
};

#define SL_PLAYSTATE_STOPPED 1
#define SL_PLAYSTATE_PAUSED  2
#define SL_PLAYSTATE_PLAYING 3

constexpr size_t kRingBufferFrames = 8192;
constexpr int kAudioQueueNumBuffers = 3;
constexpr size_t kAudioQueueFramesPerBuffer = 512;
// How full a blocking (AudioTrack-style) writer keeps the ring. A writer that blocks only
// when the ring is full keeps it full, which put kRingBufferFrames between a sound being
// mixed and heard: a third of a second at the 24 kHz a mixer commonly runs at.
constexpr double kBlockingFillSeconds = 0.08;
// Raw PCM capture cap (seconds of source audio) for KUDROID_AUDIO_DUMP.
constexpr uint64_t kDumpMaxSeconds = 10;

// PCM dumps require an explicit diagnostic opt-in.
constexpr int kDumpMaxFiles = 3;
static bool audio_dump_enabled() {
    static const bool enabled = [] {
        const char* v = std::getenv("KUDROID_AUDIO_DUMP");
        return v && std::strcmp(v, "1") == 0;
    }();
    return enabled;
}

struct AudioPlayer {
    std::mutex controlMtx;
    std::timed_mutex writeMtx;
    std::mutex mtx;
    std::condition_variable cv;
    bool shutdown = false;
    bool workerRunning = false;
    int32_t outputError = 0;
    void* callback = nullptr;   // SLAndroidSimpleBufferQueueCallback
    void* context = nullptr;
    uint32_t playState = SL_PLAYSTATE_STOPPED;
    void* iface = nullptr;
    uint32_t pendingCount = 0;

    double sampleRate = 48000.0;
    uint32_t channels = 2;
    uint32_t bitsPerSample = 16;
    bool floatSamples = false;
    float volume = 1.0f;

    uint64_t bufferCapacityBytes = 0;

    std::atomic<uint64_t> framesWritten{0};
    std::atomic<uint64_t> framesPlayed{0};

    // Source-format PCM capture (KUDROID_AUDIO_DUMP).
    std::FILE* dumpFile = nullptr;
    uint64_t dumpBytes = 0;
    uint64_t dumpHeaderBytes = 0;
    // Output-queue callback only: the previous buffer ended short, so the next one
    // starts from silence.
    bool aqWasSilent = false;

    ~AudioPlayer() {
        if (dumpFile) std::fclose(dumpFile);
    }

    // Interleaved 16-bit PCM ring buffer.
    std::vector<int16_t> ring;
    size_t ringHead = 0;
    size_t ringCount = 0;

    uint64_t framesRead = 0;
    std::deque<uint64_t> bufferEnds;
    uint32_t queueIndex = 0;
    uint64_t generation = 0;

    bool started = false;

#if defined(__APPLE__)
    AudioQueueRef aq = nullptr;
    AudioQueueBufferRef aqBuffers[kAudioQueueNumBuffers] = {nullptr};
    void* aqUserData = nullptr;
    size_t aqBufferFrames[kAudioQueueNumBuffers] = {};
    uint64_t aqBufferGeneration[kAudioQueueNumBuffers] = {};
    bool aqResetting = false;
    bool aqNeedsReset = false;
#endif
};

static std::mutex g_players_mtx;
static std::unordered_map<void*, std::shared_ptr<AudioPlayer>> g_players;
static thread_local AudioPlayer* g_audioCallbackPlayer = nullptr;
static std::atomic<bool> g_hostAudioSuspended{false};

static std::unique_lock<std::mutex> player_control_lock(AudioPlayer* p) {
    std::unique_lock<std::mutex> lock(p->controlMtx, std::defer_lock);
    if (g_audioCallbackPlayer == p) (void) lock.try_lock();
    else lock.lock();
    return lock;
}

static std::shared_ptr<AudioPlayer> find_player(void* iface) {
    std::lock_guard<std::mutex> lock(g_players_mtx);
    auto it = g_players.find(iface);
    return it != g_players.end() ? it->second : nullptr;
}

#if defined(__APPLE__)
static bool apple_activate_audio_session() {
    Class cls = objc_getClass("AVAudioSession");
    if (!cls) return false;
    SEL selShared = sel_registerName("sharedInstance");
    id session = reinterpret_cast<id (*)(Class, SEL)>(objc_msgSend)(cls, selShared);
    if (!session) return false;
    SEL selSetActive = sel_registerName("setActive:error:");
    const BOOL active = reinterpret_cast<BOOL (*)(id, SEL, BOOL, void*)>(objc_msgSend)(
        session, selSetActive, YES, nullptr);
    if (!active) std::fprintf(stderr, "[KuDroidAudio] session activation failed\n");
    return active;
}
#else
extern "C" void kudroid_audio_session_suspend(void) {}
extern "C" void kudroid_audio_session_resume(void) {}
#endif

static uint32_t player_bytes_per_frame(const AudioPlayer* p) {
    const uint32_t ch = (p->channels > 0 && p->channels <= 8) ? p->channels : 2;
    const uint32_t bytes = (p->bitsPerSample > 0 ? (p->bitsPerSample / 8) : 2) * ch;
    return bytes > 0 ? bytes : 4;
}

struct AudioRead {
    size_t frames = 0;
    size_t callbacks = 0;
    void* callback = nullptr;
    void* context = nullptr;
    uint64_t generation = 0;
};

// Called with the ring mutex held.
static AudioRead player_read_pcm(AudioPlayer* p, int16_t* out, size_t wanted) {
    AudioRead r;
    r.generation = p->generation;
    if (p->shutdown || p->playState != SL_PLAYSTATE_PLAYING ||
        g_hostAudioSuspended.load(std::memory_order_acquire)) return r;
    r.frames = std::min(p->ringCount, wanted);
    const size_t ch = p->channels;
    if (out && r.frames) {
        const size_t first = std::min(r.frames, kRingBufferFrames - p->ringHead);
        std::memcpy(out, p->ring.data() + p->ringHead * ch, first * ch * sizeof(int16_t));
        if (first < r.frames) {
            std::memcpy(out + first * ch, p->ring.data(),
                        (r.frames - first) * ch * sizeof(int16_t));
        }
    }
    p->ringHead = (p->ringHead + r.frames) % kRingBufferFrames;
    p->ringCount -= r.frames;
    p->framesRead += r.frames;
    while (!p->bufferEnds.empty() && p->bufferEnds.front() <= p->framesRead) {
        p->bufferEnds.pop_front();
        ++p->queueIndex;
        ++r.callbacks;
    }
    p->pendingCount = static_cast<uint32_t>(p->bufferEnds.size());
    r.callback = p->callback;
    r.context = p->context;
    r.generation = p->generation;
    return r;
}

static void dispatch_audio_callbacks(AudioPlayer* p, const AudioRead& read) {
    if (!read.callback) return;
    struct CallbackScope {
        AudioPlayer* previous;
        explicit CallbackScope(AudioPlayer* current) : previous(g_audioCallbackPlayer) {
            g_audioCallbackPlayer = current;
        }
        ~CallbackScope() { g_audioCallbackPlayer = previous; }
    } scope(p);
    for (size_t i = 0; i < read.callbacks; ++i) {
        {
            std::lock_guard<std::mutex> lock(p->mtx);
            if (p->shutdown || p->generation != read.generation || p->callback != read.callback) break;
        }
        reinterpret_cast<void (*)(void*, void*)>(read.callback)(p->iface, read.context);
    }
}

#if !defined(__APPLE__)
static void audio_worker(std::shared_ptr<AudioPlayer> p) {
    if (!p) return;
    while (true) {
        AudioRead read;
        uint64_t generation;
        {
            std::unique_lock<std::mutex> lock(p->mtx);
            p->cv.wait(lock, [&] {
                return p->shutdown || (p->ringCount > 0 && p->playState == SL_PLAYSTATE_PLAYING);
            });
            if (p->shutdown) return;
            generation = p->generation;
            read = player_read_pcm(p.get(), nullptr, kAudioQueueFramesPerBuffer);
        }
        p->cv.notify_all();
        std::this_thread::sleep_for(std::chrono::nanoseconds(
            static_cast<int64_t>(read.frames * 1e9 / p->sampleRate)));
        {
            std::lock_guard<std::mutex> lock(p->mtx);
            if (p->shutdown) return;
            if (generation != p->generation) continue;
            p->framesPlayed.fetch_add(read.frames, std::memory_order_relaxed);
        }
        dispatch_audio_callbacks(p.get(), read);
    }
}
#endif

#if defined(__APPLE__)
static void audio_queue_output_cb(void* userData, AudioQueueRef aq, AudioQueueBufferRef buffer) {
    const auto p = *static_cast<std::shared_ptr<AudioPlayer>*>(userData);
    if (!p) return;
    AudioRead read;
    auto* out = static_cast<int16_t*>(buffer->mAudioData);
    {
        std::lock_guard<std::mutex> lock(p->mtx);
        if (p->shutdown || p->aqResetting) return;
        for (int i = 0; i < kAudioQueueNumBuffers; ++i) {
            if (p->aqBuffers[i] != buffer) continue;
            if (p->aqBufferGeneration[i] == p->generation) {
                p->framesPlayed.fetch_add(p->aqBufferFrames[i], std::memory_order_relaxed);
            }
            read = player_read_pcm(p.get(), out, kAudioQueueFramesPerBuffer);
            p->aqBufferFrames[i] = read.frames;
            p->aqBufferGeneration[i] = p->generation;
            break;
        }
    }
    // Ramp the edges of a gap: a hard cut to zero (and a hard start after it) is a
    // broadband click at every stall.
    {
        const size_t ch = p->channels;
        constexpr size_t kRamp = 64;
        const size_t ramp = read.frames < kRamp ? read.frames : kRamp;
        if (read.frames > 0 && p->aqWasSilent) {
            for (size_t f = 0; f < ramp; ++f) {
                const int32_t gain = static_cast<int32_t>((f + 1) * 256 / ramp);
                for (size_t c = 0; c < ch; ++c) {
                    int16_t& v = out[f * ch + c];
                    v = static_cast<int16_t>((v * gain) >> 8);
                }
            }
        }
        if (read.frames > 0 && read.frames < kAudioQueueFramesPerBuffer) {
            for (size_t f = 0; f < ramp; ++f) {
                const int32_t gain = static_cast<int32_t>((ramp - f) * 256 / ramp);
                const size_t at = read.frames - ramp + f;
                for (size_t c = 0; c < ch; ++c) {
                    int16_t& v = out[at * ch + c];
                    v = static_cast<int16_t>((v * gain) >> 8);
                }
            }
        }
        p->aqWasSilent = read.frames < kAudioQueueFramesPerBuffer;
    }
    if (read.frames < kAudioQueueFramesPerBuffer) {
        std::memset(out + read.frames * p->channels, 0,
                    (kAudioQueueFramesPerBuffer - read.frames) * p->channels * sizeof(int16_t));
    }
    buffer->mAudioDataByteSize = static_cast<UInt32>(kAudioQueueFramesPerBuffer * p->channels * 2);
    p->cv.notify_all();
    const OSStatus st = AudioQueueEnqueueBuffer(aq, buffer, 0, nullptr);
    if (st != noErr) {
        std::lock_guard<std::mutex> lock(p->mtx);
        if (!p->shutdown && p->generation == read.generation) {
            p->aqNeedsReset = true;
            if (!p->aqResetting && !g_hostAudioSuspended.load(std::memory_order_acquire) &&
                st != kAudioQueueErr_EnqueueDuringReset) p->outputError = -1;
            p->cv.notify_all();
            std::fprintf(stderr, "[KuDroidAudio] enqueue failed status=%d\n", static_cast<int>(st));
        }
    }
    dispatch_audio_callbacks(p.get(), read);
}

// Called with the control mutex held; callbacks only take the ring mutex.
static bool open_audio_queue(AudioPlayer* p) {
    if (p->shutdown) return false;
    if (p->aq) return true;
    if (g_hostAudioSuspended.load(std::memory_order_acquire)) return true;
    if (!apple_activate_audio_session()) return false;
    AudioStreamBasicDescription asbd = {};
    // CoreAudio converts the source sample rate to the hardware rate.
    asbd.mSampleRate = p->sampleRate;
    asbd.mFormatID = kAudioFormatLinearPCM;
    asbd.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
    asbd.mBitsPerChannel = 16;
    asbd.mChannelsPerFrame = p->channels;
    asbd.mFramesPerPacket = 1;
    asbd.mBytesPerFrame = p->channels * 2;
    asbd.mBytesPerPacket = asbd.mBytesPerFrame;
    {
        std::lock_guard<std::mutex> lock(g_players_mtx);
        const auto it = g_players.find(p->iface);
        if (it == g_players.end()) return false;
        p->aqUserData = new std::shared_ptr<AudioPlayer>(it->second);
    }
    OSStatus st = AudioQueueNewOutput(&asbd, audio_queue_output_cb, p->aqUserData,
                                      nullptr, nullptr, 0, &p->aq);
    if (st == noErr && p->aq) {
        {
            std::lock_guard<std::mutex> lock(p->mtx);
            if (p->ring.empty()) p->ring.assign(kRingBufferFrames * p->channels, 0);
        }
        const UInt32 bytes = static_cast<UInt32>(kAudioQueueFramesPerBuffer * p->channels * 2);
        for (int i = 0; i < kAudioQueueNumBuffers && st == noErr; ++i) {
            st = AudioQueueAllocateBuffer(p->aq, bytes, &p->aqBuffers[i]);
            if (st != noErr) break;
            std::memset(p->aqBuffers[i]->mAudioData, 0, bytes);
            p->aqBuffers[i]->mAudioDataByteSize = bytes;
            p->aqBufferGeneration[i] = p->generation;
            st = AudioQueueEnqueueBuffer(p->aq, p->aqBuffers[i], 0, nullptr);
        }
        if (st == noErr) st = AudioQueueSetParameter(p->aq, kAudioQueueParam_Volume, p->volume);
        if (st == noErr && p->playState == SL_PLAYSTATE_PLAYING &&
            !g_hostAudioSuspended.load(std::memory_order_acquire)) {
            st = AudioQueueStart(p->aq, nullptr);
            p->started = st == noErr;
        }
    }
    if (st != noErr || !p->aq) {
        std::fprintf(stderr, "[KuDroidAudio] output creation failed status=%d\n", static_cast<int>(st));
        if (p->aq) AudioQueueDispose(p->aq, true);
        p->aq = nullptr;
        for (auto& buffer : p->aqBuffers) buffer = nullptr;
        delete static_cast<std::shared_ptr<AudioPlayer>*>(p->aqUserData);
        p->aqUserData = nullptr;
        return false;
    }
    std::fprintf(stderr, "[KuDroidAudio] output rate=%.0f channels=%u input_bits=%u\n",
                 p->sampleRate, p->channels, p->bitsPerSample);
    return true;
}
#endif

// Appends source-format PCM to a WAV and keeps the header sizes current, so the file
// is playable at any moment. Stops after kDumpMaxSeconds. Called under the write lock.
static void dump_pcm(AudioPlayer* p, const int16_t* pcm, size_t bytes) {
    const uint64_t cap =
        static_cast<uint64_t>(p->sampleRate) * p->channels * 2 * kDumpMaxSeconds;
    if (p->dumpBytes >= cap) {
        if (p->dumpFile) { std::fclose(p->dumpFile); p->dumpFile = nullptr; }
        return;
    }
    if (!p->dumpFile) {
        static std::atomic<int> s_dumpFiles{0};
        if (s_dumpFiles.fetch_add(1, std::memory_order_relaxed) >= kDumpMaxFiles) {
            p->dumpBytes = cap;
            return;
        }
        const char* dir = std::getenv("KUDROID_AUDIO_DUMP_DIR");
        if ((!dir || !*dir) && &g_kudroid_log_dir_ptr && g_kudroid_log_dir_ptr) {
            dir = g_kudroid_log_dir_ptr;
        }
        char path[1024];
        std::snprintf(path, sizeof(path), "%s/audio_dump_%p.wav", dir && *dir ? dir : ".",
                      static_cast<void*>(p));
        p->dumpFile = std::fopen(path, "wb");
        if (!p->dumpFile) { p->dumpBytes = cap; return; }
        std::fprintf(stderr, "[KuDroidAudio] dumping source PCM to %s rate=%.0f channels=%u\n",
                     path, p->sampleRate, p->channels);
        const uint8_t blank[44] = {};
        std::fwrite(blank, 1, sizeof(blank), p->dumpFile);
    }
    std::fwrite(pcm, 1, bytes, p->dumpFile);
    p->dumpBytes += bytes;
    // The header is rewritten about twice a second, not per write: this runs on the
    // pump thread and every seek+flush is a syscall pair on the audio path.
    const uint64_t headerEvery = static_cast<uint64_t>(p->sampleRate) * p->channels;
    if (p->dumpBytes < cap && p->dumpBytes - p->dumpHeaderBytes < headerEvery &&
        p->dumpHeaderBytes != 0) {
        return;
    }
    p->dumpHeaderBytes = p->dumpBytes;
    auto le32 = [](uint8_t* d, uint32_t v) { for (int i = 0; i < 4; ++i) d[i] = (v >> (8 * i)) & 0xff; };
    auto le16 = [](uint8_t* d, uint16_t v) { d[0] = v & 0xff; d[1] = v >> 8; };
    uint8_t h[44];
    std::memcpy(h, "RIFF", 4);
    le32(h + 4, static_cast<uint32_t>(36 + p->dumpBytes));
    std::memcpy(h + 8, "WAVEfmt ", 8);
    le32(h + 16, 16);
    le16(h + 20, 1);
    le16(h + 22, static_cast<uint16_t>(p->channels));
    le32(h + 24, static_cast<uint32_t>(p->sampleRate));
    le32(h + 28, static_cast<uint32_t>(p->sampleRate * p->channels * 2));
    le16(h + 32, static_cast<uint16_t>(p->channels * 2));
    le16(h + 34, 16);
    std::memcpy(h + 36, "data", 4);
    le32(h + 40, static_cast<uint32_t>(p->dumpBytes));
    std::fseek(p->dumpFile, 0, SEEK_SET);
    std::fwrite(h, 1, sizeof(h), p->dumpFile);
    std::fseek(p->dumpFile, 0, SEEK_END);
    std::fflush(p->dumpFile);
}

static int32_t player_write_pcm(AudioPlayer* p, const void* data, uint32_t sizeBytes,
                                bool blocking = true, bool submittedBuffer = false,
                                int64_t timeoutNanos = -1) {
    if (!p || !data || sizeBytes == 0) return 0;
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::nanoseconds(timeoutNanos > 0 ? timeoutNanos : 0);
    std::unique_lock<std::timed_mutex> writer(p->writeMtx, std::defer_lock);
    if (blocking && timeoutNanos < 0) writer.lock();
    else if (!(blocking ? writer.try_lock_until(deadline) : writer.try_lock())) {
        return submittedBuffer ? -2 : 0;
    }
    const size_t ch = p->channels;
    const size_t bpf = player_bytes_per_frame(p);
    const size_t frames = sizeBytes / bpf;
    if (!frames) return 0;
    // Reused per thread: no allocation on the steady-state write path.
    static thread_local std::vector<int16_t> converted;
    converted.resize(frames * ch);
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < converted.size(); ++i) {
        if (p->bitsPerSample == 16) {
            std::memcpy(&converted[i], bytes + i * 2, 2);
        } else if (p->bitsPerSample == 32 && p->floatSamples) {
            float value;
            std::memcpy(&value, bytes + i * 4, 4);
            if (!std::isfinite(value)) value = 0;
            converted[i] = static_cast<int16_t>(std::lrintf(std::clamp(value, -1.0f, 1.0f) * 32767.0f));
        } else if (p->bitsPerSample == 32) {
            int32_t value;
            std::memcpy(&value, bytes + i * 4, 4);
            converted[i] = static_cast<int16_t>(value / 65536);
        } else if (p->bitsPerSample == 8) {
            converted[i] = static_cast<int16_t>((static_cast<int>(bytes[i]) - 128) * 256);
        } else return -2;
    }
    if (audio_dump_enabled()) dump_pcm(p, converted.data(), converted.size() * sizeof(int16_t));
#if defined(__APPLE__)
    {
        auto control = player_control_lock(p);
        if (!control.owns_lock()) return -1;
        if (!open_audio_queue(p)) return -1;
    }
#endif
    size_t written = 0;
    std::unique_lock<std::mutex> lock(p->mtx);
    if (p->shutdown) return -3;
    if (p->outputError) return p->outputError;
    if (p->ring.empty()) p->ring.resize(kRingBufferFrames * ch);
    // Buffer-queue submissions must be accepted whole.
    if (submittedBuffer && frames > kRingBufferFrames - p->ringCount) return -2;
    const uint64_t generation = p->generation;
    // Buffer-queue submissions are whole buffers the app sized itself, non-blocking
    // writers learn the room from the return value, and a stream that is not playing yet
    // may prefill the whole ring; only a blocking write to a playing stream is held to the
    // shorter fill level.
    const size_t shortFill = std::min(
        kRingBufferFrames,
        std::max<size_t>(static_cast<size_t>(p->sampleRate * kBlockingFillSeconds),
                         2 * kAudioQueueFramesPerBuffer));
    while (written < frames) {
        if (p->shutdown || p->outputError || generation != p->generation) break;
        const size_t fillLimit =
            (blocking && !submittedBuffer && p->playState == SL_PLAYSTATE_PLAYING)
                ? shortFill
                : kRingBufferFrames;
        const size_t room = fillLimit > p->ringCount ? fillLimit - p->ringCount : 0;
        if (!room) {
            if (!blocking || p->playState != SL_PLAYSTATE_PLAYING ||
                g_hostAudioSuspended.load(std::memory_order_acquire)) break;
            if (timeoutNanos >= 0) {
                if (p->cv.wait_until(lock, deadline) == std::cv_status::timeout) break;
            } else p->cv.wait(lock);
            continue;
        }
        const size_t count = std::min(room, frames - written);
        const size_t tail = (p->ringHead + p->ringCount) % kRingBufferFrames;
        const size_t first = std::min(count, kRingBufferFrames - tail);
        std::memcpy(p->ring.data() + tail * ch, converted.data() + written * ch,
                    first * ch * sizeof(int16_t));
        if (first < count) {
            std::memcpy(p->ring.data(), converted.data() + (written + first) * ch,
                        (count - first) * ch * sizeof(int16_t));
        }
        p->ringCount += count;
        written += count;
        p->framesWritten.fetch_add(count, std::memory_order_relaxed);
        if (submittedBuffer) {
            p->bufferEnds.push_back(p->framesWritten.load(std::memory_order_relaxed));
            p->pendingCount = static_cast<uint32_t>(p->bufferEnds.size());
        }
#if !defined(__APPLE__)
        if (!p->workerRunning) {
            p->workerRunning = true;
            std::thread(audio_worker, find_player(p->iface)).detach();
        }
#endif
        p->cv.notify_all();
    }
    return written ? static_cast<int32_t>(written * bpf) :
                     p->shutdown ? -3 : p->outputError;
}

// Called with the ring mutex held.
static void reset_audio_ring(AudioPlayer* p) {
    ++p->generation;
    p->outputError = 0;
    p->ringHead = p->ringCount = 0;
    p->framesRead = 0;
    p->bufferEnds.clear();
    p->pendingCount = p->queueIndex = 0;
    p->framesWritten.store(0, std::memory_order_relaxed);
    p->framesPlayed.store(0, std::memory_order_relaxed);
    p->cv.notify_all();
}

#if defined(__APPLE__)
static OSStatus reset_audio_output(AudioPlayer* p) {
    if (!p->aq) return noErr;
    {
        std::lock_guard<std::mutex> lock(p->mtx);
        p->aqResetting = true;
        p->aqNeedsReset = true;
    }
    p->started = false;
    OSStatus st = AudioQueueStop(p->aq, true);
    if (st == noErr) st = AudioQueueReset(p->aq);
    for (int i = 0; i < kAudioQueueNumBuffers && st == noErr; ++i) {
        auto* buffer = p->aqBuffers[i];
        p->aqBufferFrames[i] = 0;
        p->aqBufferGeneration[i] = p->generation;
        std::memset(buffer->mAudioData, 0, buffer->mAudioDataBytesCapacity);
        buffer->mAudioDataByteSize = buffer->mAudioDataBytesCapacity;
        st = AudioQueueEnqueueBuffer(p->aq, buffer, 0, nullptr);
    }
    {
        std::lock_guard<std::mutex> lock(p->mtx);
        p->aqResetting = false;
        p->aqNeedsReset = st != noErr;
    }
    return st;
}

// Called with the control mutex held.
static OSStatus start_audio_output(AudioPlayer* p) {
    if (g_hostAudioSuspended.load(std::memory_order_acquire)) return noErr;
    bool needsReset;
    {
        std::lock_guard<std::mutex> lock(p->mtx);
        needsReset = p->aqNeedsReset || p->outputError != 0;
    }
    OSStatus st = needsReset ? reset_audio_output(p) : noErr;
    if (st == noErr) {
        {
            std::lock_guard<std::mutex> lock(p->mtx);
            p->outputError = 0;
        }
        st = AudioQueueStart(p->aq, nullptr);
    }
    p->started = st == noErr;
    if (st != noErr) {
        std::lock_guard<std::mutex> lock(p->mtx);
        p->outputError = -1;
        p->aqNeedsReset = true;
        std::fprintf(stderr, "[KuDroidAudio] output restart failed status=%d\n", static_cast<int>(st));
    }
    p->cv.notify_all();
    return st;
}

static std::vector<std::shared_ptr<AudioPlayer>> snapshot_audio_players() {
    std::lock_guard<std::mutex> lock(g_players_mtx);
    std::vector<std::shared_ptr<AudioPlayer>> players;
    players.reserve(g_players.size());
    for (const auto& kv : g_players) players.push_back(kv.second);
    return players;
}

extern "C" void kudroid_audio_session_suspend(void) {
    g_hostAudioSuspended.store(true, std::memory_order_release);
    size_t paused = 0;
    for (const auto& p : snapshot_audio_players()) {
        auto control = player_control_lock(p.get());
        if (!control.owns_lock()) continue;
        if (p->aq && p->started) {
            ++paused;
            const OSStatus st = AudioQueuePause(p->aq);
            if (st != noErr) {
                std::lock_guard<std::mutex> lock(p->mtx);
                p->aqNeedsReset = true;
                std::fprintf(stderr, "[KuDroidAudio] output pause failed status=%d\n", static_cast<int>(st));
            }
        }
        p->started = false;
        {
            std::lock_guard<std::mutex> lock(p->mtx);
            p->cv.notify_all();
        }
    }
    std::fprintf(stderr, "[KuDroidAudio] session suspended outputs=%zu\n", paused);
}

extern "C" void kudroid_audio_session_resume(void) {
    if (!apple_activate_audio_session()) return;
    g_hostAudioSuspended.store(false, std::memory_order_release);
    size_t resumed = 0;
    for (const auto& p : snapshot_audio_players()) {
        auto control = player_control_lock(p.get());
        if (!control.owns_lock()) continue;
        {
            std::lock_guard<std::mutex> lock(p->mtx);
            if (p->shutdown || p->playState != SL_PLAYSTATE_PLAYING) continue;
        }
        if (p->aq) {
            if (start_audio_output(p.get()) == noErr) ++resumed;
        } else if (open_audio_queue(p.get()) && p->started) ++resumed;
    }
    std::fprintf(stderr, "[KuDroidAudio] session resumed outputs=%zu\n", resumed);
}
#endif

static int32_t set_player_state(AudioPlayer* p, uint32_t state, bool reset = false) {
    auto control = player_control_lock(p);
    if (!control.owns_lock()) return -1;
    {
        std::lock_guard<std::mutex> lock(p->mtx);
        if (p->shutdown) return -3;
        p->playState = state;
        if (reset) reset_audio_ring(p);
        p->cv.notify_all();
    }
#if defined(__APPLE__)
    if (p->aq) {
        OSStatus st = reset ? reset_audio_output(p) : noErr;
        if (st == noErr && state == SL_PLAYSTATE_PLAYING &&
            !g_hostAudioSuspended.load(std::memory_order_acquire)) {
            st = start_audio_output(p);
        } else {
            if (st == noErr && p->started) st = AudioQueuePause(p->aq);
            p->started = false;
        }
        if (st != noErr) {
            std::lock_guard<std::mutex> lock(p->mtx);
            p->outputError = -1;
            p->cv.notify_all();
            return -1;
        }
    }
#endif
    return 0;
}

static void close_player(std::shared_ptr<AudioPlayer> p) {
    if (g_audioCallbackPlayer == p.get()) {
        {
            std::lock_guard<std::mutex> lock(p->mtx);
            p->shutdown = true;
            p->cv.notify_all();
        }
        // Synchronous disposal must complete off the callback thread.
        std::thread([p] { close_player(p); }).detach();
        return;
    }
    std::lock_guard<std::mutex> control(p->controlMtx);
    {
        std::lock_guard<std::mutex> lock(p->mtx);
        p->shutdown = true;
        p->cv.notify_all();
    }
#if defined(__APPLE__)
    if (p->aq) AudioQueueDispose(p->aq, true);
    p->aq = nullptr;
    for (auto& buffer : p->aqBuffers) buffer = nullptr;
    delete static_cast<std::shared_ptr<AudioPlayer>*>(p->aqUserData);
    p->aqUserData = nullptr;
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// OpenSL ES Implementations
// ─────────────────────────────────────────────────────────────────────────────

extern "C" SLresult bionic_slCreateEngine(SLObjectItf* pEngine, uint32_t numOptions,
                                          const void* pEngineOptions,
                                          uint32_t numInterfaces,
                                          const SLInterfaceID* pInterfaceIds,
                                          const void* pInterfaceRequired) {
    (void)numOptions; (void)pEngineOptions; (void)numInterfaces;
    (void)pInterfaceIds; (void)pInterfaceRequired;
    if (!pEngine) return SL_RESULT_PARAMETER_INVALID;
    static int dummyEngine = 1;
    *pEngine = &dummyEngine;
    return SL_RESULT_SUCCESS;
}

extern "C" void bionic_slObjectDestroy(SLObjectItf self) {
    if (!self) return;
    std::shared_ptr<AudioPlayer> player;
    {
        std::lock_guard<std::mutex> lock(g_players_mtx);
        auto it = g_players.find(self);
        if (it != g_players.end()) {
            player = it->second;
            g_players.erase(it);
        }
    }
    if (!player) return;

    close_player(player);
}

extern "C" SLresult bionic_slObjectRealize(SLObjectItf self, SLboolean async) {
    (void)self; (void)async;
    return SL_RESULT_SUCCESS;
}

extern "C" SLresult bionic_slObjectGetInterface(SLObjectItf self,
                                                const SLInterfaceID iid,
                                                void* pInterface) {
    (void)self; (void)iid;
    if (!pInterface) return SL_RESULT_PARAMETER_INVALID;
    *reinterpret_cast<void**>(pInterface) = self;
    return SL_RESULT_SUCCESS;
}

extern "C" SLresult bionic_slEngineCreateAudioPlayer(SLEngineItf self,
                                                     SLObjectItf* pPlayer,
                                                     const void* pSrc,
                                                     const void* pSink,
                                                     uint32_t numInterfaces,
                                                     const SLInterfaceID* pInterfaceIds,
                                                     const void* pInterfaceRequired) {
    (void)self; (void)pSrc; (void)pSink; (void)numInterfaces;
    (void)pInterfaceIds; (void)pInterfaceRequired;
    if (!pPlayer) return SL_RESULT_PARAMETER_INVALID;

    auto player = std::make_shared<AudioPlayer>();
    player->iface = player.get();

    {
        const auto* src = static_cast<const SLDataSource*>(pSrc);
        if (src && src->pFormat) {
            const auto* fmt = static_cast<const SLDataFormat_PCM*>(src->pFormat);
            if (fmt->numChannels > 0) player->channels = fmt->numChannels;
            if (fmt->samplesPerSec > 1000000) {
                player->sampleRate = static_cast<double>(fmt->samplesPerSec) / 1000.0;
            } else if (fmt->samplesPerSec > 0) {
                player->sampleRate = static_cast<double>(fmt->samplesPerSec);
            }
            if (fmt->bitsPerSample == 8 || fmt->bitsPerSample == 16 || fmt->bitsPerSample == 32) {
                player->bitsPerSample = fmt->bitsPerSample;
            }
            if (player->sampleRate < 8000.0 || player->sampleRate > 192000.0) {
                player->sampleRate = 44100.0;
            }
            if (player->channels == 0 || player->channels > 8) player->channels = 2;
        }
    }

    {
        std::lock_guard<std::mutex> lock(g_players_mtx);
        g_players[player.get()] = player;
    }
    *pPlayer = player.get();
    return SL_RESULT_SUCCESS;
}

extern "C" SLresult bionic_slEngineCreateOutputMix(SLEngineItf self,
                                                   SLObjectItf* pMix,
                                                   uint32_t numInterfaces,
                                                   const SLInterfaceID* pInterfaceIds,
                                                   const void* pInterfaceRequired) {
    (void)self; (void)numInterfaces; (void)pInterfaceIds; (void)pInterfaceRequired;
    if (!pMix) return SL_RESULT_PARAMETER_INVALID;
    static int dummyMix = 1;
    *pMix = &dummyMix;
    return SL_RESULT_SUCCESS;
}

extern "C" SLresult bionic_slAndroidSimpleBufferQueueRegisterCallback(
    SLAndroidSimpleBufferQueueItf self, void* callback, void* pContext) {
    auto player = find_player(self);
    if (!player) return SL_RESULT_PARAMETER_INVALID;
    std::lock_guard<std::mutex> lock(player->mtx);
    player->callback = callback;
    player->context = pContext;
    return SL_RESULT_SUCCESS;
}

extern "C" SLresult bionic_slAndroidSimpleBufferQueueEnqueue(
    SLAndroidSimpleBufferQueueItf self, const void* pBuffer, uint32_t size) {
    auto player = find_player(self);
    if (!player) return SL_RESULT_PARAMETER_INVALID;
    int32_t res = player_write_pcm(player.get(), pBuffer, size, false, true);
    if (res == -2) return SL_RESULT_BUFFER_INSUFFICIENT;
    if (res < 0) return SL_RESULT_RESOURCE_ERROR;
    return SL_RESULT_SUCCESS;
}

extern "C" SLresult bionic_slAndroidSimpleBufferQueueClear(
    SLAndroidSimpleBufferQueueItf self) {
    auto player = find_player(self);
    if (!player) return SL_RESULT_PARAMETER_INVALID;
    std::lock_guard<std::mutex> lock(player->mtx);
    reset_audio_ring(player.get());
    return SL_RESULT_SUCCESS;
}

extern "C" SLresult bionic_slAndroidSimpleBufferQueueGetState(
    SLAndroidSimpleBufferQueueItf self, uint32_t* pState) {
    auto player = find_player(self);
    if (!player) return SL_RESULT_PARAMETER_INVALID;
    std::lock_guard<std::mutex> lock(player->mtx);
    if (pState) {
        pState[0] = player->pendingCount;
        pState[1] = player->queueIndex;
    }
    return SL_RESULT_SUCCESS;
}

extern "C" SLresult bionic_slPlaySetPlayState(SLPlayItf self, uint32_t state) {
    auto player = find_player(self);
    if (!player) return SL_RESULT_PARAMETER_INVALID;
    if (state < SL_PLAYSTATE_STOPPED || state > SL_PLAYSTATE_PLAYING) return SL_RESULT_PARAMETER_INVALID;
    return set_player_state(player.get(), state) == 0 ? SL_RESULT_SUCCESS : SL_RESULT_RESOURCE_ERROR;
}

extern "C" SLresult bionic_slPlayGetPlayState(SLPlayItf self, uint32_t* pState) {
    auto player = find_player(self);
    if (!player) return SL_RESULT_PARAMETER_INVALID;
    std::lock_guard<std::mutex> lock(player->mtx);
    if (pState) *pState = player->playState;
    return SL_RESULT_SUCCESS;
}

extern "C" SLresult bionic_slVolumeSetVolumeLevel(SLVolumeItf self, int32_t millibel) {
    auto player = find_player(self);
    if (!player) return SL_RESULT_PARAMETER_INVALID;
    const float db = static_cast<float>(millibel) / 100.0f;
    const float linear = (db >= -150.0f) ? std::pow(10.0f, db / 20.0f) : 0.0f;
    auto control = player_control_lock(player.get());
    if (!control.owns_lock()) return SL_RESULT_RESOURCE_ERROR;
    player->volume = linear;
#if defined(__APPLE__)
    if (player->aq) AudioQueueSetParameter(player->aq, kAudioQueueParam_Volume, linear);
#endif
    return SL_RESULT_SUCCESS;
}

extern "C" SLresult bionic_slVolumeGetVolumeLevel(SLVolumeItf self, int32_t* pLevel) {
    auto player = find_player(self);
    if (!player) return SL_RESULT_PARAMETER_INVALID;
    if (pLevel) {
        const float lin = player->volume;
        if (lin <= 0.0f) *pLevel = -9600;
        else *pLevel = static_cast<int32_t>(std::round(2000.0f * std::log10(lin)));
    }
    return SL_RESULT_SUCCESS;
}

// ─────────────────────────────────────────────────────────────────────────────
// AAudio Implementations
// ─────────────────────────────────────────────────────────────────────────────
#define AAUDIO_FORMAT_PCM_I16 1
#define AAUDIO_FORMAT_PCM_FLOAT 2

struct AAudioStreamImpl {
    std::shared_ptr<AudioPlayer> player;
    int32_t sampleRate = 48000;
    int32_t channels = 2;
    int32_t format = AAUDIO_FORMAT_PCM_I16;
};

struct AAudioBuilderState {
    int32_t sampleRate = 48000;
    int32_t channels = 2;
    int32_t format = AAUDIO_FORMAT_PCM_I16;
};

constexpr int32_t kAAudioOk = 0;
constexpr int32_t kAAudioErrorInvalidHandle = -892;
constexpr int32_t kAAudioErrorUnimplemented = -906;

static std::mutex g_builderMtx;
static std::unordered_map<void*, AAudioBuilderState> g_builderStates;
static int g_builderIds = 0;
static std::mutex g_streams_mtx;
static std::unordered_map<void*, std::shared_ptr<AAudioStreamImpl>> g_streams;

static std::shared_ptr<AAudioStreamImpl> find_stream(void* stream) {
    std::lock_guard<std::mutex> lock(g_streams_mtx);
    auto it = g_streams.find(stream);
    return it != g_streams.end() ? it->second : nullptr;
}

static AAudioBuilderState* builder_state(void* builder) {
    std::lock_guard<std::mutex> lock(g_builderMtx);
    auto it = g_builderStates.find(builder);
    return it != g_builderStates.end() ? &it->second : nullptr;
}

extern "C" int32_t bionic_AAudio_createStreamBuilder(void** builder) {
    if (!builder) return 1;
    std::lock_guard<std::mutex> lock(g_builderMtx);
    void* id = reinterpret_cast<void*>(static_cast<uintptr_t>(++g_builderIds));
    g_builderStates[id] = AAudioBuilderState();
    *builder = id;
    return kAAudioOk;
}

extern "C" int32_t bionic_AAudioStreamBuilder_setDirection(void* builder, int32_t direction) {
    (void)builder; (void)direction;
    return 0;
}

extern "C" int32_t bionic_AAudioStreamBuilder_setPerformanceMode(void* builder, int32_t mode) {
    (void)builder; (void)mode;
    return 0;
}

extern "C" int32_t bionic_AAudioStreamBuilder_setSampleRate(void* builder, int32_t sampleRate) {
    if (AAudioBuilderState* st = builder_state(builder)) st->sampleRate = sampleRate;
    return 0;
}

extern "C" int32_t bionic_AAudioStreamBuilder_setChannelCount(void* builder, int32_t channelCount) {
    if (AAudioBuilderState* st = builder_state(builder)) st->channels = channelCount;
    return 0;
}

extern "C" int32_t bionic_AAudioStreamBuilder_setFormat(void* builder, int32_t format) {
    if (AAudioBuilderState* st = builder_state(builder)) st->format = format;
    return 0;
}

extern "C" int32_t bionic_AAudioStreamBuilder_setDataCallback(void* builder, void* callback, void* userData) {
    (void)builder; (void)callback; (void)userData;
    return kAAudioErrorUnimplemented;
}

extern "C" int32_t bionic_AAudioStreamBuilder_openStream(void* builder, void** stream) {
    if (!stream) return 1;
    AAudioBuilderState st;
    {
        std::lock_guard<std::mutex> lock(g_builderMtx);
        auto it = g_builderStates.find(builder);
        if (it == g_builderStates.end()) return kAAudioErrorInvalidHandle;
        st = it->second;
    }
    auto impl = std::make_shared<AAudioStreamImpl>();
    impl->sampleRate = st.sampleRate;
    impl->channels = st.channels;
    impl->format = st.format;
    auto player = std::make_shared<AudioPlayer>();
    player->iface = player.get();
    player->sampleRate = impl->sampleRate > 0 ? static_cast<double>(impl->sampleRate) : 48000.0;
    player->channels = impl->channels > 0 ? static_cast<uint32_t>(impl->channels) : 2;
    if (player->channels > 8) return -898;
    player->bitsPerSample = (impl->format == AAUDIO_FORMAT_PCM_FLOAT) ? 32 : 16;
    player->floatSamples = impl->format == AAUDIO_FORMAT_PCM_FLOAT;
    impl->player = player;
    {
        std::lock_guard<std::mutex> lock(g_streams_mtx);
        g_streams[player.get()] = impl;
    }
    {
        std::lock_guard<std::mutex> lock(g_players_mtx);
        g_players[player.get()] = player;
    }
    *stream = player.get();
    return 0;
}

extern "C" int32_t bionic_AAudioStreamBuilder_delete(void* builder) {
    std::lock_guard<std::mutex> lock(g_builderMtx);
    g_builderStates.erase(builder);
    return 0;
}

extern "C" int32_t bionic_AAudioStream_requestStart(void* stream) {
    auto impl = find_stream(stream);
    if (!impl) return 1;
    return set_player_state(impl->player.get(), SL_PLAYSTATE_PLAYING);
}

extern "C" int32_t bionic_AAudioStream_requestStop(void* stream) {
    auto impl = find_stream(stream);
    if (!impl) return 1;
    return set_player_state(impl->player.get(), SL_PLAYSTATE_STOPPED);
}

extern "C" int32_t bionic_AAudioStream_close(void* stream) {
    auto impl = find_stream(stream);
    if (!impl) return 1;
    close_player(impl->player);
    {
        std::lock_guard<std::mutex> lock(g_streams_mtx);
        g_streams.erase(stream);
    }
    {
        std::lock_guard<std::mutex> lock(g_players_mtx);
        g_players.erase(stream);
    }
    return 0;
}

extern "C" int32_t bionic_AAudioStream_getSampleRate(void* stream) {
    auto impl = find_stream(stream);
    return impl ? impl->sampleRate : 48000;
}

extern "C" int32_t bionic_AAudioStream_getChannelCount(void* stream) {
    auto impl = find_stream(stream);
    return impl ? impl->channels : 2;
}

extern "C" int32_t bionic_AAudioStream_getFormat(void* stream) {
    auto impl = find_stream(stream);
    return impl ? impl->format : AAUDIO_FORMAT_PCM_I16;
}

extern "C" int32_t bionic_AAudioStream_write(void* stream, const void* buffer,
                                             int32_t numFrames, int64_t timeoutNanoseconds) {
    auto impl = find_stream(stream);
    if (!impl) return 0;
    const size_t bytesPerFrame = ((impl->format == AAUDIO_FORMAT_PCM_FLOAT) ? 4u : 2u) *
                                 static_cast<size_t>(impl->channels > 0 ? impl->channels : 2);
    if (numFrames < 0 || static_cast<size_t>(numFrames) > INT32_MAX / bytesPerFrame) return -898;
    const uint32_t byteSize = static_cast<uint32_t>(static_cast<size_t>(numFrames) * bytesPerFrame);
    int32_t writtenBytes = player_write_pcm(impl->player.get(), buffer, byteSize,
                                           timeoutNanoseconds > 0, false, timeoutNanoseconds);
    return (writtenBytes > 0) ? (writtenBytes / static_cast<int32_t>(bytesPerFrame)) : 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// android.media.AudioTrack Native Bindings
// ─────────────────────────────────────────────────────────────────────────────

#define ANDROID_ENCODING_PCM_16BIT 2
#define ANDROID_ENCODING_PCM_8BIT  3
#define ANDROID_ENCODING_PCM_FLOAT 4

static uint32_t android_bits_per_sample(int32_t encoding) {
    switch (encoding) {
        case ANDROID_ENCODING_PCM_8BIT:  return 8;
        case ANDROID_ENCODING_PCM_FLOAT: return 32;
        case ANDROID_ENCODING_PCM_16BIT:
        default:                         return 16;
    }
}

static int32_t host_output_sample_rate() {
    return 48000;
}

extern "C" int32_t bionic_kudroid_audiotrack_min_buffer_size(int32_t sampleRateInHz,
                                                            int32_t channelCount,
                                                            int32_t encoding) {
    if (encoding < 0) return host_output_sample_rate();
    if (sampleRateInHz <= 0 || channelCount <= 0) return 0;
    const uint32_t bytesPerFrame = (android_bits_per_sample(encoding) / 8) *
                                   static_cast<uint32_t>(channelCount);
    if (bytesPerFrame == 0) return 0;
    const uint32_t frames = static_cast<uint32_t>(sampleRateInHz) / 50;
    return static_cast<int32_t>(frames * bytesPerFrame);
}

extern "C" int64_t bionic_kudroid_audiotrack_create(int32_t sampleRateInHz,
                                                    int32_t channelCount,
                                                    int32_t encoding,
                                                    int32_t bufferSizeInBytes) {
    if (sampleRateInHz <= 0 || channelCount <= 0 || channelCount > 8) return 0;

    auto player = std::make_shared<AudioPlayer>();
    player->iface = player.get();
    player->sampleRate = static_cast<double>(sampleRateInHz);
    player->channels = static_cast<uint32_t>(channelCount > 8 ? 8 : channelCount);
    player->bitsPerSample = android_bits_per_sample(encoding);
    player->floatSamples = encoding == ANDROID_ENCODING_PCM_FLOAT;
    {
        const uint32_t bpf = player_bytes_per_frame(player.get());
        const uint64_t minBytes = static_cast<uint64_t>(sampleRateInHz / 50) * bpf;
        player->bufferCapacityBytes = bufferSizeInBytes > 0
            ? std::max<uint64_t>(static_cast<uint64_t>(bufferSizeInBytes), minBytes)
            : minBytes;
    }
    {
        std::lock_guard<std::mutex> lock(g_players_mtx);
        g_players[player.get()] = player;
    }
    return static_cast<int64_t>(reinterpret_cast<uintptr_t>(player.get()));
}

static std::shared_ptr<AudioPlayer> audiotrack_find(int64_t track) {
    if (track == 0) return nullptr;
    return find_player(reinterpret_cast<void*>(static_cast<uintptr_t>(track)));
}

extern "C" int32_t bionic_kudroid_audiotrack_write(int64_t track, const void* data,
                                                   int32_t sizeInBytes) {
    return bionic_kudroid_audiotrack_write_mode(track, data, sizeInBytes, true);
}

extern "C" int32_t bionic_kudroid_audiotrack_write_mode(int64_t track, const void* data,
                                                       int32_t sizeInBytes, bool blocking) {
    auto p = audiotrack_find(track);
    if (!p || sizeInBytes < 0) return -3;  // ERROR_INVALID_OPERATION
    if (sizeInBytes == 0) return 0;
    return player_write_pcm(p.get(), data, sizeInBytes, blocking);
}

extern "C" int32_t bionic_kudroid_audiotrack_play(int64_t track) {
    auto p = audiotrack_find(track);
    return p ? set_player_state(p.get(), SL_PLAYSTATE_PLAYING, false) : -3;
}

extern "C" int32_t bionic_kudroid_audiotrack_pause(int64_t track) {
    auto p = audiotrack_find(track);
    return p ? set_player_state(p.get(), SL_PLAYSTATE_PAUSED, false) : -3;
}

extern "C" int32_t bionic_kudroid_audiotrack_stop(int64_t track) {
    auto p = audiotrack_find(track);
    return p ? set_player_state(p.get(), SL_PLAYSTATE_STOPPED, true) : -3;
}

extern "C" int32_t bionic_kudroid_audiotrack_flush(int64_t track) {
    auto p = audiotrack_find(track);
    if (!p) return -3;
    std::lock_guard<std::mutex> control(p->controlMtx);
    {
        std::lock_guard<std::mutex> lock(p->mtx);
        if (p->playState == SL_PLAYSTATE_PLAYING) return 0;
        reset_audio_ring(p.get());
    }
#if defined(__APPLE__)
    if (reset_audio_output(p.get()) != noErr) return -1;
#endif
    return 0;
}

extern "C" int32_t bionic_kudroid_audiotrack_release(int64_t track) {
    auto p = audiotrack_find(track);
    if (!p) return -3;
    void* const iface = p->iface;
    close_player(p);
    {
        std::lock_guard<std::mutex> lock(g_players_mtx);
        g_players.erase(iface);
    }
    return 0;
}

extern "C" int32_t bionic_kudroid_audiotrack_set_volume(int64_t track, float volume) {
    auto p = audiotrack_find(track);
    if (!p) return -3;
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;
    std::lock_guard<std::mutex> control(p->controlMtx);
    p->volume = volume;
#if defined(__APPLE__)
    if (p->aq) AudioQueueSetParameter(p->aq, kAudioQueueParam_Volume, volume);
#endif
    return 0;
}

extern "C" int32_t bionic_kudroid_audiotrack_head_position(int64_t track) {
    auto p = audiotrack_find(track);
    if (!p) return 0;
    const uint64_t frames = p->framesPlayed.load(std::memory_order_relaxed);
    return static_cast<int32_t>(static_cast<uint32_t>(frames));
}

extern "C" int32_t bionic_kudroid_audiotrack_latency_frames(int64_t track) {
    auto p = audiotrack_find(track);
    if (!p) return 0;
    const uint64_t written = p->framesWritten.load(std::memory_order_relaxed);
    const uint64_t played = p->framesPlayed.load(std::memory_order_relaxed);
    const uint64_t queued = written >= played ? written - played : 0;
    return static_cast<int32_t>(queued > static_cast<uint64_t>(INT32_MAX) ? INT32_MAX : queued);
}

const SymbolEntry kAudioSymbols[] = {
    // OpenSL ES
    {"slCreateEngine", reinterpret_cast<void*>(&bionic_slCreateEngine)},
    {"slObjectDestroy", reinterpret_cast<void*>(&bionic_slObjectDestroy)},
    {"slObjectRealize", reinterpret_cast<void*>(&bionic_slObjectRealize)},
    {"slObjectGetInterface", reinterpret_cast<void*>(&bionic_slObjectGetInterface)},
    {"slEngineCreateAudioPlayer", reinterpret_cast<void*>(&bionic_slEngineCreateAudioPlayer)},
    {"slEngineCreateOutputMix", reinterpret_cast<void*>(&bionic_slEngineCreateOutputMix)},
    {"slAndroidSimpleBufferQueueRegisterCallback", reinterpret_cast<void*>(&bionic_slAndroidSimpleBufferQueueRegisterCallback)},
    {"slAndroidSimpleBufferQueueEnqueue", reinterpret_cast<void*>(&bionic_slAndroidSimpleBufferQueueEnqueue)},
    {"slAndroidSimpleBufferQueueClear", reinterpret_cast<void*>(&bionic_slAndroidSimpleBufferQueueClear)},
    {"slAndroidSimpleBufferQueueGetState", reinterpret_cast<void*>(&bionic_slAndroidSimpleBufferQueueGetState)},
    {"slBufferQueueRegisterCallback", reinterpret_cast<void*>(&bionic_slAndroidSimpleBufferQueueRegisterCallback)},
    {"slBufferQueueEnqueue", reinterpret_cast<void*>(&bionic_slAndroidSimpleBufferQueueEnqueue)},
    {"slBufferQueueClear", reinterpret_cast<void*>(&bionic_slAndroidSimpleBufferQueueClear)},
    {"slBufferQueueGetState", reinterpret_cast<void*>(&bionic_slAndroidSimpleBufferQueueGetState)},
    {"slPlaySetPlayState", reinterpret_cast<void*>(&bionic_slPlaySetPlayState)},
    {"slPlayGetPlayState", reinterpret_cast<void*>(&bionic_slPlayGetPlayState)},
    {"slVolumeSetVolumeLevel", reinterpret_cast<void*>(&bionic_slVolumeSetVolumeLevel)},
    {"slVolumeGetVolumeLevel", reinterpret_cast<void*>(&bionic_slVolumeGetVolumeLevel)},

    // AAudio
    {"AAudio_createStreamBuilder", reinterpret_cast<void*>(&bionic_AAudio_createStreamBuilder)},
    {"AAudioStreamBuilder_setDirection", reinterpret_cast<void*>(&bionic_AAudioStreamBuilder_setDirection)},
    {"AAudioStreamBuilder_setPerformanceMode", reinterpret_cast<void*>(&bionic_AAudioStreamBuilder_setPerformanceMode)},
    {"AAudioStreamBuilder_setSampleRate", reinterpret_cast<void*>(&bionic_AAudioStreamBuilder_setSampleRate)},
    {"AAudioStreamBuilder_setChannelCount", reinterpret_cast<void*>(&bionic_AAudioStreamBuilder_setChannelCount)},
    {"AAudioStreamBuilder_setFormat", reinterpret_cast<void*>(&bionic_AAudioStreamBuilder_setFormat)},
    {"AAudioStreamBuilder_setDataCallback", reinterpret_cast<void*>(&bionic_AAudioStreamBuilder_setDataCallback)},
    {"AAudioStreamBuilder_openStream", reinterpret_cast<void*>(&bionic_AAudioStreamBuilder_openStream)},
    {"AAudioStreamBuilder_delete", reinterpret_cast<void*>(&bionic_AAudioStreamBuilder_delete)},
    {"AAudioStream_requestStart", reinterpret_cast<void*>(&bionic_AAudioStream_requestStart)},
    {"AAudioStream_requestStop", reinterpret_cast<void*>(&bionic_AAudioStream_requestStop)},
    {"AAudioStream_close", reinterpret_cast<void*>(&bionic_AAudioStream_close)},
    {"AAudioStream_getSampleRate", reinterpret_cast<void*>(&bionic_AAudioStream_getSampleRate)},
    {"AAudioStream_getChannelCount", reinterpret_cast<void*>(&bionic_AAudioStream_getChannelCount)},
    {"AAudioStream_getFormat", reinterpret_cast<void*>(&bionic_AAudioStream_getFormat)},
    {"AAudioStream_write", reinterpret_cast<void*>(&bionic_AAudioStream_write)},
};

} // namespace

const SymbolEntry* get_audio_symbols(size_t* count) {
    if (count) {
        *count = sizeof(kAudioSymbols) / sizeof(SymbolEntry);
    }
    return kAudioSymbols;
}

extern "C" const char* kudroid_test_audio(void) {
    std::string log = "=== KuDroid AudioShim (OpenSL ES & AAudio) Subsystem Test ===\n";

    log += "Step 1: Testing OpenSL ES slCreateEngine...\n";
    SLObjectItf engineObj = nullptr;
    SLresult res = bionic_slCreateEngine(&engineObj, 0, nullptr, 0, nullptr, nullptr);
    if (res != SL_RESULT_SUCCESS || !engineObj) {
        log += "ERROR: slCreateEngine failed with result: " + std::to_string(res) + "\n";
        return strdup(log.c_str());
    }
    log += "slCreateEngine created engine object: " + std::to_string(reinterpret_cast<uintptr_t>(engineObj)) + "\n";

    res = bionic_slObjectRealize(engineObj, 0);
    log += "slObjectRealize -> " + std::string(res == SL_RESULT_SUCCESS ? "SUCCESS" : "FAILED") + "\n";

    SLEngineItf engine = nullptr;
    res = bionic_slObjectGetInterface(engineObj, (SLInterfaceID)1 /* SL_IID_ENGINE */, &engine);
    log += "slObjectGetInterface(SL_IID_ENGINE) -> " + std::string(res == SL_RESULT_SUCCESS ? "SUCCESS" : "FAILED") + "\n";

    log += "Step 2: Testing Output Mix & Audio Player creation...\n";
    SLObjectItf outputMix = nullptr;
    res = bionic_slEngineCreateOutputMix(engine, &outputMix, 0, nullptr, nullptr);
    log += "slEngineCreateOutputMix -> " + std::string(res == SL_RESULT_SUCCESS ? "SUCCESS" : "FAILED") + "\n";

    log += "Step 3: Testing AAudio Stream Builder & Pipeline...\n";
    void* builder = nullptr;
    int ares = bionic_AAudio_createStreamBuilder(&builder);
    if (ares == 0 && builder) {
        bionic_AAudioStreamBuilder_setSampleRate(builder, 44100);
        bionic_AAudioStreamBuilder_setChannelCount(builder, 2);
        bionic_AAudioStreamBuilder_setFormat(builder, 1 /* PCM_16BIT */);
        void* stream = nullptr;
        ares = bionic_AAudioStreamBuilder_openStream(builder, &stream);
        log += "AAudioStreamBuilder_openStream -> " + std::string(ares == 0 ? "SUCCESS" : "FAILED") + "\n";
        if (stream) {
            ares = bionic_AAudioStream_requestStart(stream);
            log += "AAudioStream_requestStart -> " + std::string(ares == 0 ? "SUCCESS" : "FAILED") + "\n";

            int16_t pcmBuffer[4410 * 2];
            for (int i = 0; i < 4410; ++i) {
                int16_t val = static_cast<int16_t>(sin(2.0 * M_PI * 440.0 * i / 44100.0) * 16000.0);
                pcmBuffer[i * 2] = val;
                pcmBuffer[i * 2 + 1] = val;
            }
            int written = bionic_AAudioStream_write(stream, pcmBuffer, 4410, 0);
            log += "AAudioStream_write enqueued " + std::to_string(written) + " frames!\n";

            bionic_AAudioStream_requestStop(stream);
            bionic_AAudioStream_close(stream);
        }
        bionic_AAudioStreamBuilder_delete(builder);
    }

    log += "SUCCESS: KuDroid AudioShim initialized and verified!\n";
    return strdup(log.c_str());
}

} // namespace kudroid
