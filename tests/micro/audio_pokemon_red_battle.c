typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef int int32_t;
typedef short int16_t;
typedef unsigned long long uint64_t;
typedef long long int64_t;
typedef unsigned long size_t;
#define NULL ((void*)0)

// Bionic Android Log API
extern int __android_log_print(int priority, const char* tag, const char* fmt, ...);
#define LOG_TAG "PokemonAudio"
#define LOGI(...) __android_log_print(3, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(6, LOG_TAG, __VA_ARGS__)

// OpenSL ES Definitions
typedef int32_t SLresult;
typedef void* SLObjectItf;
typedef void* SLEngineItf;
typedef void* SLPlayItf;
typedef void* SLAndroidSimpleBufferQueueItf;
typedef void* SLInterfaceID;
typedef uint32_t SLboolean;

#define SL_RESULT_SUCCESS 0
#define SL_PLAYSTATE_STOPPED 0
#define SL_PLAYSTATE_PLAYING 2

#define SL_DATAFORMAT_PCM 3
#define SL_SAMPLINGRATE_44_1 44100000
#define SL_PCMSAMPLEFORMAT_FIXED_16 16
#define SL_CONTAINERSIZE_16 16
#define SL_SPEAKER_FRONT_LEFT 0x1
#define SL_SPEAKER_FRONT_RIGHT 0x2
#define SL_BYTEORDER_LITTLEENDIAN 2

typedef struct SLDataFormat_PCM {
    uint32_t formatType;
    uint32_t numChannels;
    uint32_t samplesPerSec;
    uint32_t bitsPerSample;
    uint32_t containerSize;
    uint32_t channelMask;
    uint32_t endianness;
} SLDataFormat_PCM;

typedef struct SLDataLocator_AndroidSimpleBufferQueue {
    uint32_t locatorType;
    uint32_t numBuffers;
} SLDataLocator_AndroidSimpleBufferQueue;

typedef struct SLDataSource {
    const void* pLocator;
    const void* pFormat;
} SLDataSource;

typedef struct SLDataLocator_OutputMix {
    uint32_t locatorType;
    SLObjectItf outputMix;
} SLDataLocator_OutputMix;

typedef struct SLDataSink {
    const void* pLocator;
    const void* pFormat;
} SLDataSink;

extern SLresult slCreateEngine(SLObjectItf* pEngine, uint32_t numOptions, const void* pEngineOptions,
                              uint32_t numInterfaces, const SLInterfaceID* pInterfaceIds, const void* pInterfaceRequired);
extern SLresult slObjectRealize(SLObjectItf self, SLboolean async);
extern SLresult slObjectGetInterface(SLObjectItf self, const SLInterfaceID iid, void* pInterface);
extern void slObjectDestroy(SLObjectItf self);
extern SLresult slEngineCreateAudioPlayer(SLEngineItf self, SLObjectItf* pPlayer, const void* pSrc,
                                         const void* pSink, uint32_t numInterfaces, const SLInterfaceID* pInterfaceIds,
                                         const void* pInterfaceRequired);
extern SLresult slEngineCreateOutputMix(SLEngineItf self, SLObjectItf* pMix, uint32_t numInterfaces,
                                       const SLInterfaceID* pInterfaceIds, const void* pInterfaceRequired);
extern SLresult slPlaySetPlayState(SLPlayItf self, uint32_t state);
extern SLresult slAndroidSimpleBufferQueueEnqueue(SLAndroidSimpleBufferQueueItf self, const void* pBuffer, uint32_t size);
extern SLresult slAndroidSimpleBufferQueueRegisterCallback(SLAndroidSimpleBufferQueueItf self, void* callback, void* pContext);
extern int usleep(unsigned int usec);

// Nhúng trực tiếp file nhạc Pokemon HGSS Champion Red Battle 11.0s PCM vào .rodata
__asm__(
    ".section .rodata\n"
    ".global g_pokemon_audio_start\n"
    ".global g_pokemon_audio_end\n"
    "g_pokemon_audio_start:\n"
    ".incbin \"assets/pokemon_red_battle_11s.raw\"\n"
    "g_pokemon_audio_end:\n"
);

extern const uint8_t g_pokemon_audio_start[];
extern const uint8_t g_pokemon_audio_end[];

#define CHUNK_BYTES 35280 // 0.2s of 44.1kHz 16-bit Stereo PCM (44100 * 4 * 0.2)
static volatile uint32_t g_streamOffset = 0;
static volatile uint32_t g_totalAudioBytes = 0;
static volatile uint32_t g_chunksStreamed = 0;

static void pokemonAudioCallback(SLAndroidSimpleBufferQueueItf bq, void* context) {
    (void)context;
    g_chunksStreamed++;

    if (g_streamOffset < g_totalAudioBytes) {
        uint32_t remaining = g_totalAudioBytes - g_streamOffset;
        uint32_t toSend = (remaining < CHUNK_BYTES) ? remaining : CHUNK_BYTES;

        const uint8_t* ptr = g_pokemon_audio_start + g_streamOffset;
        g_streamOffset += toSend;

        slAndroidSimpleBufferQueueEnqueue(bq, ptr, toSend);
    }
}

int kudroid_test_main(void) {
    LOGI("=================================================");
    LOGI("🔴 [Pokemon HGSS - Champion Red Battle Theme (11s)]");
    LOGI("=================================================");

    g_totalAudioBytes = (uint32_t)(g_pokemon_audio_end - g_pokemon_audio_start);
    g_streamOffset = 0;
    g_chunksStreamed = 0;

    LOGI("📦 Embedded Audio Track Size: %u bytes (~11.0 seconds @ 44.1kHz Stereo)", g_totalAudioBytes);
    if (g_totalAudioBytes == 0) {
        LOGE("❌ Audio binary is empty!");
        return 1;
    }

    // 1. Khởi tạo Engine
    SLObjectItf engineObj = NULL;
    slCreateEngine(&engineObj, 0, NULL, 0, NULL, NULL);
    slObjectRealize(engineObj, 0);

    SLEngineItf engine = NULL;
    slObjectGetInterface(engineObj, (SLInterfaceID)1, &engine);

    SLObjectItf outputMix = NULL;
    slEngineCreateOutputMix(engine, &outputMix, 0, NULL, NULL);
    slObjectRealize(outputMix, 0);

    // 2. Cấu hình Audio Player (44.1kHz 16-bit Stereo)
    SLDataLocator_AndroidSimpleBufferQueue loc_bufq = { 1, 4 };
    SLDataFormat_PCM format_pcm = {
        .formatType = SL_DATAFORMAT_PCM,
        .numChannels = 2,
        .samplesPerSec = SL_SAMPLINGRATE_44_1,
        .bitsPerSample = SL_PCMSAMPLEFORMAT_FIXED_16,
        .containerSize = SL_CONTAINERSIZE_16,
        .channelMask = SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT,
        .endianness = SL_BYTEORDER_LITTLEENDIAN
    };
    SLDataSource audioSrc = { &loc_bufq, &format_pcm };

    SLDataLocator_OutputMix loc_outmix = { 2, outputMix };
    SLDataSink audioSnk = { &loc_outmix, NULL };

    SLObjectItf playerObj = NULL;
    slEngineCreateAudioPlayer(engine, &playerObj, &audioSrc, &audioSnk, 0, NULL, NULL);
    slObjectRealize(playerObj, 0);

    SLPlayItf playerPlay = NULL;
    slObjectGetInterface(playerObj, (SLInterfaceID)2, &playerPlay);

    SLAndroidSimpleBufferQueueItf playerBufferQueue = NULL;
    slObjectGetInterface(playerObj, (SLInterfaceID)3, &playerBufferQueue);
    slAndroidSimpleBufferQueueRegisterCallback(playerBufferQueue, (void*)pokemonAudioCallback, NULL);

    LOGI("🎵 Starting Pokemon Red Battle Music Playback on iPhone Speaker...");
    slPlaySetPlayState(playerPlay, SL_PLAYSTATE_PLAYING);

    // Nạp trước 2 chunk đầu tiên (Double-Buffering)
    for (int i = 0; i < 2 && g_streamOffset < g_totalAudioBytes; ++i) {
        uint32_t remaining = g_totalAudioBytes - g_streamOffset;
        uint32_t toSend = (remaining < CHUNK_BYTES) ? remaining : CHUNK_BYTES;

        const uint8_t* ptr = g_pokemon_audio_start + g_streamOffset;
        g_streamOffset += toSend;

        slAndroidSimpleBufferQueueEnqueue(playerBufferQueue, ptr, toSend);
    }

    // Vòng lặp chờ bài nhạc phát hết 11 giây
    while (g_streamOffset < g_totalAudioBytes) {
        usleep(200000); // 200ms poll
    }

    // Chờ 1 giây cuối bài hát vang hết
    usleep(1200000);

    LOGI("✔ Playback completed! Streamed %u chunks (%u bytes) to CoreAudio AudioQueue!",
         g_chunksStreamed, g_totalAudioBytes);

    // Dọn dẹp
    slPlaySetPlayState(playerPlay, SL_PLAYSTATE_STOPPED);
    slObjectDestroy(playerObj);
    slObjectDestroy(outputMix);
    slObjectDestroy(engineObj);

    LOGI("=================================================");
    LOGI("🎉 POKEMON RED BATTLE THEME PLAYBACK COMPLETED 100%!");
    LOGI("=================================================");
    return 0;
}
