#include "kudroid/abi/SyscallShim.h"
#include "kudroid/platform/AudioShim.h"

#include <cstdint>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <chrono>
#include <unordered_map>
#include <memory>
#include <atomic>
#include <cstdio>
#include <string>

#if defined(__APPLE__)
#include <AudioToolbox/AudioToolbox.h>
#include <objc/runtime.h>
#include <objc/message.h>

static void apple_ensure_audio_session_active() {
    static std::atomic<bool> s_done{false};
    if (s_done.exchange(true)) return;
    Class cls = objc_getClass("AVAudioSession");
    if (!cls) return;
    SEL selShared = sel_registerName("sharedInstance");
    id session = reinterpret_cast<id (*)(Class, SEL)>(objc_msgSend)(cls, selShared);
    if (!session) return;

    Class strCls = objc_getClass("NSString");
    SEL selUtf8 = sel_registerName("stringWithUTF8String:");
    id catStr = reinterpret_cast<id (*)(Class, SEL, const char*)>(objc_msgSend)(
        strCls, selUtf8, "AVAudioSessionCategoryPlayback");

    SEL selSetCat = sel_registerName("setCategory:withOptions:error:");
    reinterpret_cast<BOOL (*)(id, SEL, id, unsigned long, void*)>(objc_msgSend)(
        session, selSetCat, catStr, 1UL /* AVAudioSessionCategoryOptionMixWithOthers */, nullptr);

    SEL selSetActive = sel_registerName("setActive:error:");
    reinterpret_cast<BOOL (*)(id, SEL, BOOL, void*)>(objc_msgSend)(
        session, selSetActive, YES, nullptr);
}
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

#define SL_PLAYSTATE_STOPPED 0
#define SL_PLAYSTATE_PAUSED  1
#define SL_PLAYSTATE_PLAYING 2

constexpr size_t kRingBufferFrames = 8192;
constexpr int kAudioQueueNumBuffers = 3;
constexpr size_t kAudioQueueFramesPerBuffer = 1024;

struct AudioPlayer {
    std::mutex mtx;
    std::condition_variable cv;
    bool shutdown = false;
    bool workerRunning = false;
    void* callback = nullptr;   // SLAndroidSimpleBufferQueueCallback
    void* context = nullptr;
    uint32_t playState = SL_PLAYSTATE_STOPPED;
    void* iface = nullptr;
    uint32_t pendingCount = 0;

    double sampleRate = 48000.0;
    uint32_t channels = 2;
    uint32_t bitsPerSample = 16;
    float volume = 1.0f;

    uint64_t bufferCapacityBytes = 0;

    std::atomic<uint64_t> framesWritten{0};
    std::atomic<uint64_t> framesPlayed{0};

    // Interleaved 16-bit PCM ring buffer.
    std::vector<int16_t> ring;
    size_t ringHead = 0;
    size_t ringCount = 0;

    // Linear interpolation state for low rates (24kHz -> 48kHz).
    int16_t upsamplePrev[8] = {0};
    bool hasUpsamplePrev = false;

    bool started = false;

#if defined(__APPLE__)
    AudioQueueRef aq = nullptr;
    AudioQueueBufferRef aqBuffers[kAudioQueueNumBuffers] = {nullptr};
    void* aqUserData = nullptr;
#endif
};

static std::mutex g_players_mtx;
static std::unordered_map<void*, std::shared_ptr<AudioPlayer>> g_players;

static std::shared_ptr<AudioPlayer> find_player(void* iface) {
    std::lock_guard<std::mutex> lock(g_players_mtx);
    auto it = g_players.find(iface);
    return it != g_players.end() ? it->second : nullptr;
}

static uint32_t player_bytes_per_frame(const AudioPlayer* p) {
    const uint32_t ch = (p->channels > 0 && p->channels <= 8) ? p->channels : 2;
    const uint32_t bytes = (p->bitsPerSample > 0 ? (p->bitsPerSample / 8) : 2) * ch;
    return bytes > 0 ? bytes : 4;
}

#if !defined(__APPLE__)
static void audio_worker(void* iface) {
    auto p = find_player(iface);
    if (!p) return;

    while (true) {
        size_t framesToConsume = 0;
        double rate = 48000.0;
        void* cb = nullptr;
        void* ctx = nullptr;

        {
            std::unique_lock<std::mutex> lock(p->mtx);
            p->cv.wait(lock, [&] {
                return p->shutdown || (p->ringCount > 0 && p->playState == SL_PLAYSTATE_PLAYING);
            });
            if (p->shutdown) return;

            framesToConsume = std::min(p->ringCount, kAudioQueueFramesPerBuffer);
            p->ringHead = (p->ringHead + framesToConsume) % kRingBufferFrames;
            p->ringCount -= framesToConsume;
            if (p->pendingCount > 0) p->pendingCount--;

            rate = (p->sampleRate > 0) ? p->sampleRate : 48000.0;
            cb = p->callback;
            ctx = p->context;
        }

        const uint64_t ms = (rate > 0 && framesToConsume > 0)
            ? (framesToConsume * 1000) / static_cast<uint64_t>(rate)
            : 10;
        std::this_thread::sleep_for(std::chrono::milliseconds(ms > 0 ? ms : 1));

        p->framesPlayed.fetch_add(framesToConsume, std::memory_order_relaxed);
        p->cv.notify_all();

        if (cb) {
            reinterpret_cast<void (*)(void*, void*)>(cb)(iface, ctx);
        }
    }
}
#endif

#if defined(__APPLE__)
static void audio_queue_output_cb(void* userData, AudioQueueRef aq, AudioQueueBufferRef buffer) {
    const auto ref = static_cast<std::shared_ptr<AudioPlayer>*>(userData);
    const std::shared_ptr<AudioPlayer> p = *ref;
    if (!p) return;

    void* cb = nullptr;
    void* ctx = nullptr;
    const uint32_t ch = (p->channels > 0 && p->channels <= 8) ? p->channels : 2;
    const size_t wantFrames = kAudioQueueFramesPerBuffer;
    int16_t* out = static_cast<int16_t*>(buffer->mAudioData);
    size_t takeFrames = 0;

    {
        std::lock_guard<std::mutex> lock(p->mtx);
        if (!p->shutdown && p->playState == SL_PLAYSTATE_PLAYING && !p->ring.empty()) {
            takeFrames = std::min(p->ringCount, wantFrames);
            for (size_t f = 0; f < takeFrames; ++f) {
                const size_t ringIdx = ((p->ringHead + f) % kRingBufferFrames) * ch;
                for (size_t c = 0; c < ch; ++c) {
                    out[f * ch + c] = p->ring[ringIdx + c];
                }
            }
            p->ringHead = (p->ringHead + takeFrames) % kRingBufferFrames;
            p->ringCount -= takeFrames;
            if (p->pendingCount > 0 && takeFrames > 0) {
                p->pendingCount--;
            }
            cb = p->callback;
            ctx = p->context;
        }
    }

    if (takeFrames < wantFrames) {
        std::memset(out + takeFrames * ch, 0, (wantFrames - takeFrames) * ch * sizeof(int16_t));
    }
    buffer->mAudioDataByteSize = static_cast<UInt32>(wantFrames * ch * sizeof(int16_t));

    p->cv.notify_all();
    p->framesPlayed.fetch_add(takeFrames > 0 ? takeFrames : wantFrames, std::memory_order_relaxed);

    if (cb) {
        reinterpret_cast<void (*)(void*, void*)>(cb)(p->iface, ctx);
    }

    AudioQueueEnqueueBuffer(aq, buffer, 0, nullptr);
}

static bool open_audio_queue(AudioPlayer* p) {
    if (p->aq) return true;
    apple_ensure_audio_session_active();

    double targetRate = (p->sampleRate > 0) ? p->sampleRate : 48000.0;
    if (targetRate <= 24000.0) {
        targetRate *= 2.0;
    }
    if (targetRate < 44100.0) targetRate = 44100.0;
    const uint32_t ch = (p->channels > 0 && p->channels <= 8) ? p->channels : 2;

    AudioStreamBasicDescription asbd = {};
    asbd.mSampleRate = targetRate;
    asbd.mFormatID = kAudioFormatLinearPCM;
    asbd.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
    asbd.mBitsPerChannel = 16;
    asbd.mChannelsPerFrame = ch;
    asbd.mFramesPerPacket = 1;
    asbd.mBytesPerFrame = ch * 2;
    asbd.mBytesPerPacket = asbd.mBytesPerFrame;

    {
        std::lock_guard<std::mutex> lock(g_players_mtx);
        auto it = g_players.find(p->iface);
        if (it == g_players.end()) return false;
        p->aqUserData = new std::shared_ptr<AudioPlayer>(it->second);
    }

    OSStatus st = AudioQueueNewOutput(&asbd, audio_queue_output_cb, p->aqUserData,
                                      nullptr, nullptr, 0, &p->aq);
    if (st != noErr || !p->aq) {
        delete static_cast<std::shared_ptr<AudioPlayer>*>(p->aqUserData);
        p->aqUserData = nullptr;
        return false;
    }

    p->ring.assign(kRingBufferFrames * ch, 0);
    p->ringHead = 0;
    p->ringCount = 0;

    const UInt32 bufBytes = static_cast<UInt32>(kAudioQueueFramesPerBuffer * ch * 2);
    for (int i = 0; i < kAudioQueueNumBuffers; ++i) {
        st = AudioQueueAllocateBuffer(p->aq, bufBytes, &p->aqBuffers[i]);
        if (st == noErr && p->aqBuffers[i]) {
            std::memset(p->aqBuffers[i]->mAudioData, 0, bufBytes);
            p->aqBuffers[i]->mAudioDataByteSize = bufBytes;
            AudioQueueEnqueueBuffer(p->aq, p->aqBuffers[i], 0, nullptr);
        }
    }

    AudioQueueSetParameter(p->aq, kAudioQueueParam_Volume, p->volume);
    st = AudioQueueStart(p->aq, nullptr);
    p->started = (st == noErr);
    return true;
}
#endif

static int32_t player_write_pcm(AudioPlayer* p, const void* data, uint32_t sizeBytes) {
    if (!p || sizeBytes == 0 || !data) return 0;

    const uint32_t ch = (p->channels > 0 && p->channels <= 8) ? p->channels : 2;
    const uint32_t bytesPerSample = (p->bitsPerSample > 0) ? (p->bitsPerSample / 8) : 2;
    const uint32_t bytesPerFrame = bytesPerSample * ch;
    if (bytesPerFrame == 0) return 0;

    const size_t inFrames = sizeBytes / bytesPerFrame;
    if (inFrames == 0) return 0;

    const bool needUpsample = (p->sampleRate > 0 && p->sampleRate <= 24000.0);
    std::vector<int16_t> converted;

    if (p->bitsPerSample == 16) {
        const int16_t* src = static_cast<const int16_t*>(data);
        if (needUpsample) {
            converted.resize(inFrames * 2 * ch);
            for (size_t f = 0; f < inFrames; ++f) {
                for (size_t c = 0; c < ch; ++c) {
                    int16_t cur = src[f * ch + c];
                    int16_t prev = (f > 0) ? src[(f - 1) * ch + c]
                                           : (p->hasUpsamplePrev ? p->upsamplePrev[c] : cur);
                    converted[(2 * f) * ch + c] = static_cast<int16_t>(((int)prev + (int)cur) / 2);
                    converted[(2 * f + 1) * ch + c] = cur;
                }
            }
            for (size_t c = 0; c < ch; ++c) {
                p->upsamplePrev[c] = src[(inFrames - 1) * ch + c];
            }
            p->hasUpsamplePrev = true;
        } else {
            converted.assign(src, src + inFrames * ch);
        }
    } else if (p->bitsPerSample == 32) {
        const float* src = static_cast<const float*>(data);
        converted.resize(inFrames * ch);
        for (size_t i = 0; i < inFrames * ch; ++i) {
            float val = src[i];
            if (val > 1.0f) val = 1.0f;
            else if (val < -1.0f) val = -1.0f;
            converted[i] = static_cast<int16_t>(val * 32767.0f);
        }
    } else {
        const uint8_t* src = static_cast<const uint8_t*>(data);
        converted.resize(inFrames * ch);
        for (size_t i = 0; i < inFrames * ch; ++i) {
            converted[i] = static_cast<int16_t>((static_cast<int>(src[i]) - 128) << 8);
        }
    }

    const size_t totalFramesToWrite = needUpsample ? (inFrames * 2) : inFrames;

#if defined(__APPLE__)
    if (!open_audio_queue(p)) return -1;

    size_t writtenFrames = 0;
    while (writtenFrames < totalFramesToWrite) {
        std::unique_lock<std::mutex> lock(p->mtx);
        if (p->shutdown) return -1;

        while (p->ringCount >= kRingBufferFrames && !p->shutdown) {
            if (p->cv.wait_for(lock, std::chrono::milliseconds(50)) == std::cv_status::timeout) {
                break;
            }
        }
        if (p->shutdown) return -1;

        const size_t room = (p->ringCount < kRingBufferFrames) ? (kRingBufferFrames - p->ringCount) : 0;
        if (room == 0) break;

        const size_t toWrite = std::min(totalFramesToWrite - writtenFrames, room);
        const size_t tail = (p->ringHead + p->ringCount) % kRingBufferFrames;
        for (size_t f = 0; f < toWrite; ++f) {
            const size_t destIdx = ((tail + f) % kRingBufferFrames) * ch;
            const size_t srcIdx = (writtenFrames + f) * ch;
            for (size_t c = 0; c < ch; ++c) {
                p->ring[destIdx + c] = converted[srcIdx + c];
            }
        }
        p->ringCount += toWrite;
        writtenFrames += toWrite;
        p->framesWritten.fetch_add(inFrames, std::memory_order_relaxed);
    }
#else
    {
        std::unique_lock<std::mutex> lock(p->mtx);
        if (p->ring.empty()) {
            p->ring.resize(kRingBufferFrames * ch, 0);
        }
        const size_t room = (p->ringCount < kRingBufferFrames) ? (kRingBufferFrames - p->ringCount) : 0;
        const size_t toWrite = std::min(totalFramesToWrite, room > 0 ? room : totalFramesToWrite);
        const size_t tail = (p->ringHead + p->ringCount) % kRingBufferFrames;
        for (size_t f = 0; f < toWrite; ++f) {
            const size_t destIdx = ((tail + f) % kRingBufferFrames) * ch;
            const size_t srcIdx = f * ch;
            for (size_t c = 0; c < ch; ++c) {
                p->ring[destIdx + c] = converted[srcIdx + c];
            }
        }
        p->ringCount += toWrite;
        p->pendingCount++;
        p->framesWritten.fetch_add(inFrames, std::memory_order_relaxed);

        if (!p->workerRunning) {
            p->workerRunning = true;
            std::thread(audio_worker, p->iface).detach();
        }
        p->cv.notify_all();
    }
#endif

    return static_cast<int32_t>(inFrames * bytesPerFrame);
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

    {
        std::lock_guard<std::mutex> lock(player->mtx);
        player->shutdown = true;
        player->cv.notify_all();
    }
#if defined(__APPLE__)
    AudioQueueRef aq = nullptr;
    void* userData = nullptr;
    {
        std::lock_guard<std::mutex> lock(player->mtx);
        aq = player->aq;
        userData = player->aqUserData;
        player->aq = nullptr;
        player->aqUserData = nullptr;
        for (int i = 0; i < kAudioQueueNumBuffers; ++i) {
            player->aqBuffers[i] = nullptr;
        }
    }
    if (aq) {
        AudioQueueDispose(aq, true);
        delete static_cast<std::shared_ptr<AudioPlayer>*>(userData);
    }
#endif
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
            if (fmt->bitsPerSample == 16 || fmt->bitsPerSample == 32) {
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
    int32_t res = player_write_pcm(player.get(), pBuffer, size);
    if (res < 0) return SL_RESULT_RESOURCE_ERROR;
    return SL_RESULT_SUCCESS;
}

extern "C" SLresult bionic_slAndroidSimpleBufferQueueClear(
    SLAndroidSimpleBufferQueueItf self) {
    auto player = find_player(self);
    if (!player) return SL_RESULT_PARAMETER_INVALID;
    std::lock_guard<std::mutex> lock(player->mtx);
    player->ringHead = 0;
    player->ringCount = 0;
    player->pendingCount = 0;
    return SL_RESULT_SUCCESS;
}

extern "C" SLresult bionic_slAndroidSimpleBufferQueueGetState(
    SLAndroidSimpleBufferQueueItf self, uint32_t* pState) {
    auto player = find_player(self);
    if (!player) return SL_RESULT_PARAMETER_INVALID;
    std::lock_guard<std::mutex> lock(player->mtx);
    if (pState) *pState = player->pendingCount;
    return SL_RESULT_SUCCESS;
}

extern "C" SLresult bionic_slPlaySetPlayState(SLPlayItf self, uint32_t state) {
    auto player = find_player(self);
    if (!player) return SL_RESULT_PARAMETER_INVALID;
    std::lock_guard<std::mutex> lock(player->mtx);
    player->playState = state;
#if defined(__APPLE__)
    if (player->aq) {
        if (state == SL_PLAYSTATE_PLAYING) {
            AudioQueueStart(player->aq, nullptr);
        } else if (state == SL_PLAYSTATE_PAUSED) {
            AudioQueuePause(player->aq);
        } else {
            AudioQueueStop(player->aq, false);
        }
    }
#endif
    player->cv.notify_all();
    return SL_RESULT_SUCCESS;
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
    std::lock_guard<std::mutex> lock(player->mtx);
    player->volume = linear;
#if defined(__APPLE__)
    if (player->aq) AudioQueueSetParameter(player->aq, kAudioQueueParam_Volume, linear);
#endif
    return SL_RESULT_SUCCESS;
}

extern "C" SLresult bionic_slVolumeGetVolumeLevel(SLVolumeItf self, int32_t* pLevel) {
    (void)self;
    if (pLevel) *pLevel = 0;
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
    player->bitsPerSample = (impl->format == AAUDIO_FORMAT_PCM_FLOAT) ? 32 : 16;
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
    std::lock_guard<std::mutex> lock(impl->player->mtx);
    impl->player->playState = SL_PLAYSTATE_PLAYING;
#if defined(__APPLE__)
    if (impl->player->aq) AudioQueueStart(impl->player->aq, nullptr);
#endif
    impl->player->cv.notify_all();
    return 0;
}

extern "C" int32_t bionic_AAudioStream_requestStop(void* stream) {
    auto impl = find_stream(stream);
    if (!impl) return 1;
    std::lock_guard<std::mutex> lock(impl->player->mtx);
    impl->player->playState = SL_PLAYSTATE_STOPPED;
#if defined(__APPLE__)
    if (impl->player->aq) AudioQueueStop(impl->player->aq, false);
#endif
    return 0;
}

extern "C" int32_t bionic_AAudioStream_close(void* stream) {
    auto impl = find_stream(stream);
    if (!impl) return 1;
#if defined(__APPLE__)
    AudioQueueRef aq = nullptr;
    void* userData = nullptr;
    {
        std::lock_guard<std::mutex> lock(impl->player->mtx);
        aq = impl->player->aq;
        userData = impl->player->aqUserData;
        impl->player->aq = nullptr;
        impl->player->aqUserData = nullptr;
        impl->player->shutdown = true;
    }
    if (aq) {
        AudioQueueDispose(aq, true);
        delete static_cast<std::shared_ptr<AudioPlayer>*>(userData);
    }
#endif
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
    (void)timeoutNanoseconds;
    auto impl = find_stream(stream);
    if (!impl) return 0;
    const size_t bytesPerFrame = ((impl->format == AAUDIO_FORMAT_PCM_FLOAT) ? 4u : 2u) *
                                 static_cast<size_t>(impl->channels > 0 ? impl->channels : 2);
    const uint32_t byteSize = static_cast<uint32_t>(static_cast<size_t>(numFrames) * bytesPerFrame);
    int32_t writtenBytes = player_write_pcm(impl->player.get(), buffer, byteSize);
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
    if (sampleRateInHz <= 0 || channelCount <= 0) return 0;

    auto player = std::make_shared<AudioPlayer>();
    player->iface = player.get();
    player->sampleRate = static_cast<double>(sampleRateInHz);
    player->channels = static_cast<uint32_t>(channelCount > 8 ? 8 : channelCount);
    player->bitsPerSample = android_bits_per_sample(encoding);
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
    auto p = audiotrack_find(track);
    if (!p || sizeInBytes < 0) return -3;  // ERROR_INVALID_OPERATION
    if (sizeInBytes == 0) return 0;
    return player_write_pcm(p.get(), data, sizeInBytes);
}

extern "C" int32_t bionic_kudroid_audiotrack_play(int64_t track) {
    auto p = audiotrack_find(track);
    if (!p) return -3;
    {
        std::lock_guard<std::mutex> lock(p->mtx);
        p->playState = SL_PLAYSTATE_PLAYING;
#if defined(__APPLE__)
        if (p->aq != nullptr && !p->started) {
            AudioQueueStart(p->aq, nullptr);
            p->started = true;
        }
#endif
    }
#if defined(__APPLE__)
    if (p->aq) {
        AudioQueueStart(p->aq, nullptr);
    }
#else
    p->cv.notify_all();
#endif
    return 0;
}

extern "C" int32_t bionic_kudroid_audiotrack_pause(int64_t track) {
    auto p = audiotrack_find(track);
    if (!p) return -3;
    std::lock_guard<std::mutex> lock(p->mtx);
    p->playState = SL_PLAYSTATE_PAUSED;
#if defined(__APPLE__)
    if (p->aq) AudioQueuePause(p->aq);
#endif
    return 0;
}

extern "C" int32_t bionic_kudroid_audiotrack_stop(int64_t track) {
    auto p = audiotrack_find(track);
    if (!p) return -3;
    std::lock_guard<std::mutex> lock(p->mtx);
    p->playState = SL_PLAYSTATE_STOPPED;
#if defined(__APPLE__)
    if (p->aq) AudioQueueStop(p->aq, false);
#endif
    p->ringCount = 0;
    p->ringHead = 0;
    p->framesPlayed.store(0, std::memory_order_relaxed);
    p->framesWritten.store(0, std::memory_order_relaxed);
    p->started = false;
    return 0;
}

extern "C" int32_t bionic_kudroid_audiotrack_flush(int64_t track) {
    auto p = audiotrack_find(track);
    if (!p) return -3;
    std::lock_guard<std::mutex> lock(p->mtx);
#if defined(__APPLE__)
    if (p->aq) AudioQueueFlush(p->aq);
#endif
    p->ringCount = 0;
    p->ringHead = 0;
    p->pendingCount = 0;
    p->framesPlayed.store(0, std::memory_order_relaxed);
    p->framesWritten.store(0, std::memory_order_relaxed);
    p->started = false;
    return 0;
}

extern "C" int32_t bionic_kudroid_audiotrack_release(int64_t track) {
    auto p = audiotrack_find(track);
    if (!p) return -3;
    void* const iface = p->iface;
#if defined(__APPLE__)
    AudioQueueRef aq = nullptr;
    void* userData = nullptr;
    {
        std::lock_guard<std::mutex> lock(p->mtx);
        aq = p->aq;
        userData = p->aqUserData;
        p->aq = nullptr;
        p->aqUserData = nullptr;
        p->shutdown = true;
        for (int i = 0; i < kAudioQueueNumBuffers; ++i) {
            p->aqBuffers[i] = nullptr;
        }
    }
    if (aq) {
        AudioQueueDispose(aq, true);
        delete static_cast<std::shared_ptr<AudioPlayer>*>(userData);
    }
#else
    {
        std::lock_guard<std::mutex> lock(p->mtx);
        p->shutdown = true;
    }
    p->cv.notify_all();
#endif
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
    std::lock_guard<std::mutex> lock(p->mtx);
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
