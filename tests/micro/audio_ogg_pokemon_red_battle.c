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
#define LOG_TAG "OggPokemon"
#define LOGI(...) __android_log_print(3, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(6, LOG_TAG, __VA_ARGS__)

// Include stb_vorbis implementation
#define STB_VORBIS_NO_STDIO
#include "stb_vorbis.c"

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

// Nhúng trực tiếp file nén OGG Vorbis 208KB vào .rodata
__asm__(
    ".section .rodata\n"
    ".global g_pokemon_ogg_start\n"
    ".global g_pokemon_ogg_end\n"
    "g_pokemon_ogg_start:\n"
    ".incbin \"assets/pokemon_red_battle_11s.ogg\"\n"
    "g_pokemon_ogg_end:\n"
);

extern const uint8_t g_pokemon_ogg_start[];
extern const uint8_t g_pokemon_ogg_end[];

#define CHUNK_BYTES 35280 // 0.2s of 44.1kHz 16-bit Stereo PCM
static int16_t* g_decodedPcm = NULL;
static volatile uint32_t g_streamOffset = 0;
static volatile uint32_t g_totalPcmBytes = 0;
static volatile uint32_t g_chunksStreamed = 0;

static void pokemonOggAudioCallback(SLAndroidSimpleBufferQueueItf bq, void* context) {
    (void)context;
    g_chunksStreamed++;

    if (g_streamOffset < g_totalPcmBytes && g_decodedPcm != NULL) {
        uint32_t remaining = g_totalPcmBytes - g_streamOffset;
        uint32_t toSend = (remaining < CHUNK_BYTES) ? remaining : CHUNK_BYTES;

        const uint8_t* ptr = ((const uint8_t*)g_decodedPcm) + g_streamOffset;
        g_streamOffset += toSend;

        slAndroidSimpleBufferQueueEnqueue(bq, ptr, toSend);
    }
}

int kudroid_test_main(void) {
    LOGI("=================================================");
    LOGI("🔴 [Pokemon HGSS - Red Battle 11s OGG VORBIS Test]");
    LOGI("=================================================");

    uint32_t oggSize = (uint32_t)(g_pokemon_ogg_end - g_pokemon_ogg_start);
    LOGI("📦 Embedded Compressed OGG Vorbis Size: %u bytes (Only ~208 KB!)", oggSize);

    // 1. Giải mã Ogg Vorbis trực tiếp từ bộ nhớ bằng stb_vorbis
    LOGI("⏳ [STAGE 1] Decoding OGG Vorbis file in memory via stb_vorbis...");
    int channels = 0;
    int sampleRate = 0;
    int numSamples = stb_vorbis_decode_memory(g_pokemon_ogg_start, oggSize, &channels, &sampleRate, &g_decodedPcm);

    if (numSamples <= 0 || !g_decodedPcm) {
        LOGE("❌ Failed to decode Ogg Vorbis audio!");
        return 1;
    }

    g_totalPcmBytes = (uint32_t)(numSamples * channels * sizeof(int16_t));
    g_streamOffset = 0;
    g_chunksStreamed = 0;

    LOGI("✔ [STAGE 1] OGG Vorbis Decoded Successfully! Channels: %d, SampleRate: %d Hz, PCM Size: %u bytes (~11.0s)",
         channels, sampleRate, g_totalPcmBytes);

    // 2. Khởi tạo OpenSL ES Engine
    LOGI("⏳ [STAGE 2] Initializing OpenSL ES Engine & Output Mix...");
    SLObjectItf engineObj = NULL;
    slCreateEngine(&engineObj, 0, NULL, 0, NULL, NULL);
    slObjectRealize(engineObj, 0);

    SLEngineItf engine = NULL;
    slObjectGetInterface(engineObj, (SLInterfaceID)1, &engine);

    SLObjectItf outputMix = NULL;
    slEngineCreateOutputMix(engine, &outputMix, 0, NULL, NULL);
    slObjectRealize(outputMix, 0);

    // 3. Cấu hình Audio Player
    LOGI("⏳ [STAGE 3] Creating PCM Audio Player & BufferQueue...");
    SLDataLocator_AndroidSimpleBufferQueue loc_bufq = { 1, 4 };
    SLDataFormat_PCM format_pcm = {
        .formatType = SL_DATAFORMAT_PCM,
        .numChannels = (uint32_t)channels,
        .samplesPerSec = (uint32_t)(sampleRate * 1000), // milliHz
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
    slAndroidSimpleBufferQueueRegisterCallback(playerBufferQueue, (void*)pokemonOggAudioCallback, NULL);

    LOGI("🎶 [STAGE 4] Starting Pokemon Red Battle Music Playback on iPhone Speaker...");
    slPlaySetPlayState(playerPlay, SL_PLAYSTATE_PLAYING);

    // Nạp trước 2 chunk đầu tiên (Double-Buffering)
    for (int i = 0; i < 2 && g_streamOffset < g_totalPcmBytes; ++i) {
        uint32_t remaining = g_totalPcmBytes - g_streamOffset;
        uint32_t toSend = (remaining < CHUNK_BYTES) ? remaining : CHUNK_BYTES;

        const uint8_t* ptr = ((const uint8_t*)g_decodedPcm) + g_streamOffset;
        g_streamOffset += toSend;

        slAndroidSimpleBufferQueueEnqueue(playerBufferQueue, ptr, toSend);
    }

    // Vòng lặp chờ bài nhạc phát hết 11 giây
    while (g_streamOffset < g_totalPcmBytes) {
        usleep(200000); // 200ms poll
    }

    // Chờ 1.2s cuối bài hát vang hết
    usleep(1200000);

    LOGI("✔ [STAGE 5] Playback completed! Streamed %u chunks (%u bytes PCM) to CoreAudio AudioQueue!",
         g_chunksStreamed, g_totalPcmBytes);

    // 4. Dọn dẹp
    slPlaySetPlayState(playerPlay, SL_PLAYSTATE_STOPPED);
    slObjectDestroy(playerObj);
    slObjectDestroy(outputMix);
    slObjectDestroy(engineObj);

    if (g_decodedPcm) {
        free(g_decodedPcm);
        g_decodedPcm = NULL;
    }

    LOGI("=================================================");
    LOGI("🎉 OGG VORBIS DECODE & PLAYBACK PASSED 100%!");
    LOGI("=================================================");
    return 0;
}
