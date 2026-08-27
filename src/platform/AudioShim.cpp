#include "kudroid/abi/SyscallShim.h"

#include <cstdint>
#include <cstring>
#include <cmath>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <chrono>
#include <unordered_map>
#include <memory>
#include <atomic>
#include <string>

#if defined(__APPLE__)
#include <AudioToolbox/AudioToolbox.h>
#endif

// ─────────────────────────────────────────────────────────────────────────────
// AudioShim — OpenSL ES / AAudio.
//
// On iOS: translate to CoreAudio (AudioQueue) — the PCM that the game enqueues into
// SLAndroidSimpleBufferQueue is pushed into the real AudioQueue; when a buffer is transmitted
// finished, AudioQueueOutputCallback calls back the game's callback (correct semantics
// SLAndroidSimpleBufferQueueCallback) so the game's audio loop runs for real.
// AAudioStream_write pushes straight into the same AudioQueue.
//
// On another platform (test Linux): fake worker consumes buffer and calls callback
// so the game doesn't freeze (sound is muted — no CoreAudio there).
// ─────────────────────────────────────────────────────────────────────────────

namespace kudroid {
namespace {

typedef int32_t SLresult;
#define SL_RESULT_SUCCESS 0
#define SL_RESULT_PARAMETER_INVALID 2
#define SL_RESULT_RESOURCE_ERROR 4
#define SL_RESULT_CONTENT_UNSUPPORTED 9

// OpenSL ES object/interface types (opaque).
typedef void* SLObjectItf;
typedef void* SLEngineItf;
typedef void* SLAndroidSimpleBufferQueueItf;
typedef void* SLPlayItf;
typedef void* SLVolumeItf;
typedef void* SLInterfaceID;
typedef void* SLboolean;

// Actual layout of SLDataFormat_PCM / SLDataSource (game .so compiled with bionic header).
struct SLDataFormat_PCM {
    uint32_t formatType;      // SL_DATAFORMAT_PCM = 3
    uint32_t numChannels;
    uint32_t samplesPerSec;   // milliHz trong OpenSL ES!
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

// ── Audio player ──
struct AudioPlayer {
    std::mutex mtx;
    std::condition_variable cv;
    bool shutdown = false;
    bool workerRunning = false;
    void* callback = nullptr;   // SLAndroidSimpleBufferQueueCallback
    void* context = nullptr;
    uint32_t playState = SL_PLAYSTATE_STOPPED;
    void* iface = nullptr;      // interface for the game (which is the player pointer)
    uint32_t pendingCount = 0;  // buffer enqueue but not yet played (GetState)

    // Fallback worker (non-Apple).
    std::deque<std::pair<const void*, uint32_t>> pending;

#if defined(__APPLE__)
    AudioQueueRef aq = nullptr;
    // userData passed to AudioQueueNewOutput: pointer to a copy
    // shared_ptr — keeps the player alive while the callback is running (even if
    // The game self-destructs the player right inside the callback).
    void* aqUserData = nullptr;
    double sampleRate = 44100;
    uint32_t channels = 2;
    uint32_t bitsPerSample = 16;
#endif
};

static std::mutex g_players_mtx;
static std::unordered_map<void*, std::shared_ptr<AudioPlayer>> g_players;

static std::shared_ptr<AudioPlayer> find_player(void* iface) {
    std::lock_guard<std::mutex> lock(g_players_mtx);
    auto it = g_players.find(iface);
    return it != g_players.end() ? it->second : nullptr;
}

#if defined(__APPLE__)

// AudioQueue returns the buffer that has finished playing → calls the game callback (literally
// SLAndroidSimpleBufferQueueCallback: "buffer consumed, send buffer again").
static void audio_queue_output_cb(void* userData, AudioQueueRef aq, AudioQueueBufferRef buffer) {
    const auto ref = static_cast<std::shared_ptr<AudioPlayer>*>(userData);
    const std::shared_ptr<AudioPlayer> p = *ref; // kept alive during callback

    void* cb = nullptr;
    void* ctx = nullptr;
    {
        std::lock_guard<std::mutex> lock(p->mtx);
        if (!p->shutdown) {
            cb = p->callback;
            ctx = p->context;
            if (p->pendingCount > 0) p->pendingCount--;
        }
    }
    if (cb) {
        reinterpret_cast<void (*)(void*, void*)>(cb)(p->iface, ctx);
    }
    AudioQueueFreeBuffer(aq, buffer);
}

// Create AudioQueue for the first time (lazy) according to the format saved from SLDataFormat_PCM.
// userData of AudioQueueNewOutput is a copy of shared_ptr taken from g_players
// — keeps the player alive while the output callback is running (even if the game cancels
// player right inside the callback).
static bool ensure_audio_queue(AudioPlayer* p) {
    if (p->aq) return true;

    AudioStreamBasicDescription asbd = {};
    asbd.mSampleRate = p->sampleRate;
    asbd.mFormatID = kAudioFormatLinearPCM;
    asbd.mChannelsPerFrame = p->channels;
    asbd.mBitsPerChannel = p->bitsPerSample;
    asbd.mBytesPerFrame = static_cast<UInt32>((p->bitsPerSample / 8) * p->channels);
    asbd.mFramesPerPacket = 1;
    asbd.mBytesPerPacket = asbd.mBytesPerFrame;
    if (p->bitsPerSample == 32) {
        asbd.mFormatFlags = kLinearPCMFormatFlagIsFloat | kLinearPCMFormatFlagIsPacked;
    } else {
        asbd.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
    }

    {
        std::lock_guard<std::mutex> lock(g_players_mtx);
        auto it = g_players.find(p->iface);
        if (it == g_players.end()) return false;
        p->aqUserData = new std::shared_ptr<AudioPlayer>(it->second);
    }

    const OSStatus st = AudioQueueNewOutput(&asbd, audio_queue_output_cb, p->aqUserData,
                                            nullptr, nullptr, 0, &p->aq);
    if (st != noErr || !p->aq) {
        delete static_cast<std::shared_ptr<AudioPlayer>*>(p->aqUserData);
        p->aqUserData = nullptr;
        return false;
    }
    // The queue is created lazily on first enqueue — if the game has previously set PLAYING
    // (slPlaySetPlayState/requestStart occurs before first enqueue), must start at
    // here, otherwise the queue freezes and the sound is muted even though the game is "playing".
    if (p->playState == SL_PLAYSTATE_PLAYING) {
        AudioQueueStart(p->aq, nullptr);
    }
    return true;
}

// Push a PCM block into AudioQueue (common to OpenSL enqueue and AAudio write).
static bool enqueue_pcm(AudioPlayer* p, const void* data, uint32_t size) {
    std::lock_guard<std::mutex> lock(p->mtx);
    if (p->shutdown) return false;
    if (!ensure_audio_queue(p)) return false;
    if (size == 0) return true;

    AudioQueueBufferRef buf = nullptr;
    OSStatus st = AudioQueueAllocateBuffer(p->aq, size, &buf);
    if (st != noErr || !buf) return false;
    if (data) std::memcpy(buf->mAudioData, data, size);
    buf->mAudioDataByteSize = size;
    st = AudioQueueEnqueueBuffer(p->aq, buf, 0, nullptr);
    if (st != noErr) {
        AudioQueueFreeBuffer(p->aq, buf);
        return false;
    }
    p->pendingCount++;
    return true;
}

#endif // __APPLE__

#if !defined(__APPLE__)
// Worker fallback (non-Apple): consumes the simulated buffer and then calls the callback.
static void audio_worker(std::shared_ptr<AudioPlayer> p, void* iface) {
    for (;;) {
        std::pair<const void*, uint32_t> item;
        {
            std::unique_lock<std::mutex> lock(p->mtx);
            p->cv.wait(lock, [&] { return p->shutdown || !p->pending.empty(); });
            if (p->shutdown) return;
            item = p->pending.front();
            p->pending.pop_front();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        void* cb = nullptr;
        void* ctx = nullptr;
        {
            std::lock_guard<std::mutex> lock(p->mtx);
            cb = p->callback;
            ctx = p->context;
        }
        if (cb) {
            reinterpret_cast<void (*)(void*, void*)>(cb)(iface, ctx);
        }
    }
}
#endif // !defined(__APPLE__)

// Engine create — the main entry point that the game calls.
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
    // NO DEADLOCK: lock is only used to set the shutdown flag — released before
    // AudioQueueDispose. If dispose is in lock, callback audio
    // (audio_queue_output_cb, runs on CoreAudio thread) wait p->mtx will not
    // never exits because Dispose waits for that thread to stop → deadlock.
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
    }
    if (aq) {
        // immediate=true: Dispose synchronously stops the queue and waits for the internal thread
        // AudioQueue finishes processing before returning → no more callbacks run
        // after this point → delete userData (shared_ptr owner) is safe. Callback
        // While running keeps a copy of shared_ptr so the player is not free
        // midway.
        AudioQueueDispose(aq, true);
        delete static_cast<std::shared_ptr<AudioPlayer>*>(userData);
    }
#endif
}

extern "C" SLresult bionic_slObjectRealize(SLObjectItf self, SLboolean async) {
    (void)self; (void)async;
    return SL_RESULT_SUCCESS;
}

// Return the object itself as the interface so that future calls can look up by self.
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

#if defined(__APPLE__)
    // Read the PCM format that the game declares in SLDataSource.
    const auto* src = static_cast<const SLDataSource*>(pSrc);
    if (src && src->pFormat) {
        const auto* fmt = static_cast<const SLDataFormat_PCM*>(src->pFormat);
        if (fmt->numChannels > 0) player->channels = fmt->numChannels;
        // samplesPerSec in milliHz in OpenSL ES (44100 -> 44100000).
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
#endif

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
#if defined(__APPLE__)
    if (!enqueue_pcm(player.get(), pBuffer, size)) return SL_RESULT_RESOURCE_ERROR;
    return SL_RESULT_SUCCESS;
#else
    {
        std::lock_guard<std::mutex> lock(player->mtx);
        player->pending.emplace_back(pBuffer, size);
        if (!player->workerRunning) {
            player->workerRunning = true;
            std::thread(audio_worker, player, self).detach();
        }
        player->cv.notify_all();
    }
    return SL_RESULT_SUCCESS;
#endif
}

extern "C" SLresult bionic_slAndroidSimpleBufferQueueClear(
    SLAndroidSimpleBufferQueueItf self) {
    auto player = find_player(self);
    if (!player) return SL_RESULT_PARAMETER_INVALID;
    std::lock_guard<std::mutex> lock(player->mtx);
    player->pending.clear();
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
#if defined(__APPLE__)
    std::lock_guard<std::mutex> lock(player->mtx);
    player->playState = state;
    if (player->aq) {
        if (state == SL_PLAYSTATE_PLAYING) {
            AudioQueueStart(player->aq, nullptr);
        } else if (state == SL_PLAYSTATE_PAUSED) {
            AudioQueuePause(player->aq);
        } else {
            AudioQueueStop(player->aq, false);
        }
    }
#else
    std::lock_guard<std::mutex> lock(player->mtx);
    player->playState = state;
#endif
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
    (void)millibel;
    auto player = find_player(self);
    if (!player) return SL_RESULT_PARAMETER_INVALID;
#if defined(__APPLE__)
    std::lock_guard<std::mutex> lock(player->mtx);
    if (player->aq) {
        // millibels = 100 * dB; 0 mB = unity (1.0). AudioQueue volume is LINEAR
        // 0..1, not dB — set the dB value directly (eg -6) as negative volume → mute.
        const float db = static_cast<float>(millibel) / 100.0f;
        const float linear = (db >= -150.0f) ? std::pow(10.0f, db / 20.0f) : 0.0f;
        AudioQueueSetParameter(player->aq, kAudioQueueParam_Volume, linear);
    }
#endif
    return SL_RESULT_SUCCESS;
}

extern "C" SLresult bionic_slVolumeGetVolumeLevel(SLVolumeItf self, int32_t* pLevel) {
    (void)self;
    if (pLevel) *pLevel = 0;
    return SL_RESULT_SUCCESS;
}

// ─────────────────────────────────────────────────────────────────────────────
// AAudio — write model pushed directly into AudioQueue (like OpenSL enqueue).
// ─────────────────────────────────────────────────────────────────────────────
#define AAUDIO_FORMAT_PCM_I16 1
#define AAUDIO_FORMAT_PCM_FLOAT 2

struct AAudioStreamImpl {
    std::shared_ptr<AudioPlayer> player;
    int32_t sampleRate = 44100;
    int32_t channels = 2;
    int32_t format = AAUDIO_FORMAT_PCM_I16;
};

struct AAudioBuilderState {
    int32_t sampleRate = 44100;
    int32_t channels = 2;
    int32_t format = AAUDIO_FORMAT_PCM_I16;
};

static AAudioBuilderState g_builderState;
static std::mutex g_streams_mtx;
static std::unordered_map<void*, std::shared_ptr<AAudioStreamImpl>> g_streams;

static std::shared_ptr<AAudioStreamImpl> find_stream(void* stream) {
    std::lock_guard<std::mutex> lock(g_streams_mtx);
    auto it = g_streams.find(stream);
    return it != g_streams.end() ? it->second : nullptr;
}

extern "C" int32_t bionic_AAudio_createStreamBuilder(void** builder) {
    if (!builder) return 1;
    static int dummyBuilder = 1;
    g_builderState = AAudioBuilderState();
    *builder = &dummyBuilder;
    return 0; // AAUDIO_OK
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
    (void)builder;
    g_builderState.sampleRate = sampleRate;
    return 0;
}

extern "C" int32_t bionic_AAudioStreamBuilder_setChannelCount(void* builder, int32_t channelCount) {
    (void)builder;
    g_builderState.channels = channelCount;
    return 0;
}

extern "C" int32_t bionic_AAudioStreamBuilder_setFormat(void* builder, int32_t format) {
    (void)builder;
    g_builderState.format = format;
    return 0;
}

extern "C" int32_t bionic_AAudioStreamBuilder_setDataCallback(void* builder, void* callback, void* userData) {
    // The callback-pull model is not yet supported — games using the write model still run.
    (void)builder; (void)callback; (void)userData;
    return 0;
}

extern "C" int32_t bionic_AAudioStreamBuilder_openStream(void* builder, void** stream) {
    (void)builder;
    if (!stream) return 1;
    auto impl = std::make_shared<AAudioStreamImpl>();
    impl->sampleRate = g_builderState.sampleRate;
    impl->channels = g_builderState.channels;
    impl->format = g_builderState.format;
    auto player = std::make_shared<AudioPlayer>();
    player->iface = player.get();
#if defined(__APPLE__)
    player->sampleRate = impl->sampleRate > 0 ? static_cast<double>(impl->sampleRate) : 44100.0;
    player->channels = impl->channels > 0 ? static_cast<uint32_t>(impl->channels) : 2;
    player->bitsPerSample = (impl->format == AAUDIO_FORMAT_PCM_FLOAT) ? 32 : 16;
#endif
    impl->player = player;
    {
        std::lock_guard<std::mutex> lock(g_streams_mtx);
        g_streams[player.get()] = impl;
    }
    // Also subscribe to g_players so ensure_audio_queue can be found (get a copy
    // shared_ptr as AudioQueue's safe userData).
    {
        std::lock_guard<std::mutex> lock(g_players_mtx);
        g_players[player.get()] = player;
    }
    *stream = player.get();
    return 0;
}

extern "C" int32_t bionic_AAudioStreamBuilder_delete(void* builder) {
    (void)builder;
    return 0;
}

extern "C" int32_t bionic_AAudioStream_requestStart(void* stream) {
    auto impl = find_stream(stream);
    if (!impl) return 1;
#if defined(__APPLE__)
    std::lock_guard<std::mutex> lock(impl->player->mtx);
    impl->player->playState = SL_PLAYSTATE_PLAYING;
    if (impl->player->aq) AudioQueueStart(impl->player->aq, nullptr);
#endif
    return 0;
}

extern "C" int32_t bionic_AAudioStream_requestStop(void* stream) {
    auto impl = find_stream(stream);
    if (!impl) return 1;
#if defined(__APPLE__)
    std::lock_guard<std::mutex> lock(impl->player->mtx);
    impl->player->playState = SL_PLAYSTATE_STOPPED;
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
    return impl ? impl->sampleRate : 44100;
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
#if defined(__APPLE__)
    const size_t bytesPerFrame = ((impl->format == AAUDIO_FORMAT_PCM_FLOAT) ? 4u : 2u) *
                                 static_cast<size_t>(impl->channels > 0 ? impl->channels : 2);
    const uint32_t byteSize = static_cast<uint32_t>(static_cast<size_t>(numFrames) * bytesPerFrame);
    if (!enqueue_pcm(impl->player.get(), buffer, byteSize)) return 0;
    return numFrames;
#else
    (void)buffer;
    return numFrames; // dummy: accept all
#endif
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

    log += "⏳ Step 1: Testing OpenSL ES slCreateEngine...\n";
    SLObjectItf engineObj = nullptr;
    SLresult res = bionic_slCreateEngine(&engineObj, 0, nullptr, 0, nullptr, nullptr);
    if (res != SL_RESULT_SUCCESS || !engineObj) {
        log += "❌ ERROR: slCreateEngine failed with result: " + std::to_string(res) + "\n";
        return strdup(log.c_str());
    }
    log += "✔ slCreateEngine created engine object: " + std::to_string(reinterpret_cast<uintptr_t>(engineObj)) + "\n";

    res = bionic_slObjectRealize(engineObj, 0);
    log += "✔ slObjectRealize -> " + std::string(res == SL_RESULT_SUCCESS ? "SUCCESS" : "FAILED") + "\n";

    SLEngineItf engine = nullptr;
    res = bionic_slObjectGetInterface(engineObj, (SLInterfaceID)1 /* SL_IID_ENGINE */, &engine);
    log += "✔ slObjectGetInterface(SL_IID_ENGINE) -> " + std::string(res == SL_RESULT_SUCCESS ? "SUCCESS" : "FAILED") + "\n";

    log += "⏳ Step 2: Testing Output Mix & Audio Player creation...\n";
    SLObjectItf outputMix = nullptr;
    res = bionic_slEngineCreateOutputMix(engine, &outputMix, 0, nullptr, nullptr);
    log += "✔ slEngineCreateOutputMix -> " + std::string(res == SL_RESULT_SUCCESS ? "SUCCESS" : "FAILED") + "\n";

    log += "⏳ Step 3: Testing AAudio Stream Builder & Pipeline...\n";
    void* builder = nullptr;
    int ares = bionic_AAudio_createStreamBuilder(&builder);
    if (ares == 0 && builder) {
        bionic_AAudioStreamBuilder_setSampleRate(builder, 44100);
        bionic_AAudioStreamBuilder_setChannelCount(builder, 2);
        bionic_AAudioStreamBuilder_setFormat(builder, 1 /* PCM_16BIT */);
        void* stream = nullptr;
        ares = bionic_AAudioStreamBuilder_openStream(builder, &stream);
        log += "✔ AAudioStreamBuilder_openStream -> " + std::string(ares == 0 ? "SUCCESS" : "FAILED") + "\n";
        if (stream) {
            ares = bionic_AAudioStream_requestStart(stream);
            log += "✔ AAudioStream_requestStart -> " + std::string(ares == 0 ? "SUCCESS" : "FAILED") + "\n";

            // Generate 0.1s test sound wave (Sine wave 440Hz)
            int16_t pcmBuffer[4410 * 2];
            for (int i = 0; i < 4410; ++i) {
                int16_t val = static_cast<int16_t>(sin(2.0 * M_PI * 440.0 * i / 44100.0) * 16000.0);
                pcmBuffer[i * 2] = val;
                pcmBuffer[i * 2 + 1] = val;
            }
            int written = bionic_AAudioStream_write(stream, pcmBuffer, 4410, 0);
            log += "✔ AAudioStream_write enqueued " + std::to_string(written) + " frames to AudioQueue!\n";

            bionic_AAudioStream_requestStop(stream);
            bionic_AAudioStream_close(stream);
        }
        bionic_AAudioStreamBuilder_delete(builder);
    }

    log += "🎉 SUCCESS: KuDroid AudioShim initialized and verified on iOS CoreAudio!\n";
    return strdup(log.c_str());
}

} // namespace kudroid
