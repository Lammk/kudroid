#include "kudroid/shims/SyscallShim.h"
#include <cstdint>
#include <cstring>

// ─────────────────────────────────────────────────────────────────────────────
// AudioShim — OpenSL ES / AAudio stubs.
//
// Real audio (via miniaudio → CoreAudio) is a future enhancement. For now we
// provide stubs so apps that dlopen("libOpenSLES.so") / dlopen("libaaudio.so")
// don't crash. The stubs return failure/defaults so the app falls back to
// silent operation instead of faulting.
//
// TODO: Replace these stubs with a real miniaudio-backed implementation.
// ─────────────────────────────────────────────────────────────────────────────

namespace kudroid {
namespace {

// OpenSL ES result codes.
typedef int32_t SLresult;
#define SL_RESULT_SUCCESS 0
#define SL_RESULT_PRECONDITIONS_VIOLATED 1
#define SL_RESULT_PARAMETER_INVALID 2
#define SL_RESULT_MEMORY_FAILURE 3
#define SL_RESULT_RESOURCE_ERROR 4
#define SL_RESULT_RESOURCE_LOST 5
#define SL_RESULT_IO_ERROR 6
#define SL_RESULT_BUFFER_INSUFFICIENT 7
#define SL_RESULT_CONTENT_CORRUPTED 8
#define SL_RESULT_CONTENT_UNSUPPORTED 9
#define SL_RESULT_CONTENT_NOT_FOUND 10
#define SL_RESULT_PERMISSION_DENIED 11
#define SL_RESULT_FEATURE_UNSUPPORTED 12
#define SL_RESULT_UNKNOWN_ERROR 13

// OpenSL ES object/interface types (opaque).
typedef void* SLObjectItf;
typedef void* SLEngineItf;
typedef void* SLAndroidSimpleBufferQueueItf;
typedef void* SLPlayItf;
typedef void* SLVolumeItf;
typedef void* SLDataLocator_AndroidSimpleBufferQueue;
typedef void* SLDataFormat_PCM;
typedef void* SLDataSource;
typedef void* SLDataSink;
typedef void* SLInterfaceID;
typedef void* SLboolean;

// Engine create — the main entry point apps call.
extern "C" SLresult bionic_slCreateEngine(SLObjectItf* pEngine, uint32_t numOptions,
                                          const void* pEngineOptions,
                                          uint32_t numInterfaces,
                                          const SLInterfaceID* pInterfaceIds,
                                          const void* pInterfaceRequired) {
    (void)numOptions; (void)pEngineOptions; (void)numInterfaces;
    (void)pInterfaceIds; (void)pInterfaceRequired;
    if (!pEngine) return SL_RESULT_PARAMETER_INVALID;
    // Return a non-null dummy engine object.
    static int dummyEngine = 1;
    *pEngine = &dummyEngine;
    return SL_RESULT_SUCCESS;
}

// Object destroy.
extern "C" void bionic_slObjectDestroy(SLObjectItf self) {
    (void)self;
}

// Object realize.
extern "C" SLresult bionic_slObjectRealize(SLObjectItf self, SLboolean async) {
    (void)self; (void)async;
    return SL_RESULT_SUCCESS;
}

// Object get interface.
extern "C" SLresult bionic_slObjectGetInterface(SLObjectItf self,
                                                const SLInterfaceID iid,
                                                void* pInterface) {
    (void)self; (void)iid;
    if (!pInterface) return SL_RESULT_PARAMETER_INVALID;
    // Return a dummy interface pointer.
    static int dummyInterface = 1;
    *reinterpret_cast<void**>(pInterface) = &dummyInterface;
    return SL_RESULT_SUCCESS;
}

// Engine create audio player.
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
    static int dummyPlayer = 1;
    *pPlayer = &dummyPlayer;
    return SL_RESULT_SUCCESS;
}

// Engine create output mix.
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

// Buffer queue register callback.
extern "C" SLresult bionic_slAndroidSimpleBufferQueueRegisterCallback(
    SLAndroidSimpleBufferQueueItf self, void* callback, void* pContext) {
    (void)self; (void)callback; (void)pContext;
    return SL_RESULT_SUCCESS;
}

// Buffer queue enqueue.
extern "C" SLresult bionic_slAndroidSimpleBufferQueueEnqueue(
    SLAndroidSimpleBufferQueueItf self, const void* pBuffer, uint32_t size) {
    (void)self; (void)pBuffer; (void)size;
    return SL_RESULT_SUCCESS;
}

// Buffer queue clear.
extern "C" SLresult bionic_slAndroidSimpleBufferQueueClear(
    SLAndroidSimpleBufferQueueItf self) {
    (void)self;
    return SL_RESULT_SUCCESS;
}

// Buffer queue get state.
extern "C" SLresult bionic_slAndroidSimpleBufferQueueGetState(
    SLAndroidSimpleBufferQueueItf self, uint32_t* pState) {
    (void)self;
    if (pState) *pState = 0;
    return SL_RESULT_SUCCESS;
}

// Play set play state.
extern "C" SLresult bionic_slPlaySetPlayState(SLPlayItf self, uint32_t state) {
    (void)self; (void)state;
    return SL_RESULT_SUCCESS;
}

// Play get play state.
extern "C" SLresult bionic_slPlayGetPlayState(SLPlayItf self, uint32_t* pState) {
    (void)self;
    if (pState) *pState = 0;
    return SL_RESULT_SUCCESS;
}

// Volume set volume level.
extern "C" SLresult bionic_slVolumeSetVolumeLevel(SLVolumeItf self, int32_t millibel) {
    (void)self; (void)millibel;
    return SL_RESULT_SUCCESS;
}

// Volume get volume level.
extern "C" SLresult bionic_slVolumeGetVolumeLevel(SLVolumeItf self, int32_t* pLevel) {
    (void)self;
    if (pLevel) *pLevel = 0;
    return SL_RESULT_SUCCESS;
}

// ─────────────────────────────────────────────────────────────────────────────
// AAudio stubs
// ─────────────────────────────────────────────────────────────────────────────

extern "C" int32_t bionic_AAudio_createStreamBuilder(void** builder) {
    if (!builder) return 1;
    static int dummyBuilder = 1;
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
    (void)builder; (void)sampleRate;
    return 0;
}

extern "C" int32_t bionic_AAudioStreamBuilder_setChannelCount(void* builder, int32_t channelCount) {
    (void)builder; (void)channelCount;
    return 0;
}

extern "C" int32_t bionic_AAudioStreamBuilder_setFormat(void* builder, int32_t format) {
    (void)builder; (void)format;
    return 0;
}

extern "C" int32_t bionic_AAudioStreamBuilder_setDataCallback(void* builder, void* callback, void* userData) {
    (void)builder; (void)callback; (void)userData;
    return 0;
}

extern "C" int32_t bionic_AAudioStreamBuilder_openStream(void* builder, void** stream) {
    (void)builder;
    if (!stream) return 1;
    static int dummyStream = 1;
    *stream = &dummyStream;
    return 0;
}

extern "C" int32_t bionic_AAudioStreamBuilder_delete(void* builder) {
    (void)builder;
    return 0;
}

extern "C" int32_t bionic_AAudioStream_requestStart(void* stream) {
    (void)stream;
    return 0;
}

extern "C" int32_t bionic_AAudioStream_requestStop(void* stream) {
    (void)stream;
    return 0;
}

extern "C" int32_t bionic_AAudioStream_close(void* stream) {
    (void)stream;
    return 0;
}

extern "C" int32_t bionic_AAudioStream_getSampleRate(void* stream) {
    (void)stream;
    return 44100;
}

extern "C" int32_t bionic_AAudioStream_getChannelCount(void* stream) {
    (void)stream;
    return 2;
}

extern "C" int32_t bionic_AAudioStream_getFormat(void* stream) {
    (void)stream;
    return 1; // AAUDIO_FORMAT_PCM_I16
}

extern "C" int32_t bionic_AAudioStream_write(void* stream, const void* buffer, int32_t numFrames, int64_t timeoutNanoseconds) {
    (void)stream; (void)buffer; (void)timeoutNanoseconds;
    return numFrames;
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

} // namespace kudroid
