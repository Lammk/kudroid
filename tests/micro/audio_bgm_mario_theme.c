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
#define LOG_TAG "AudioMario"
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

// Tần số các nốt nhạc nốt cơ bản (Hz)
#define NOTE_REST 0.0f
#define NOTE_C4   261.63f
#define NOTE_D4   293.66f
#define NOTE_E4   329.63f
#define NOTE_F4   349.23f
#define NOTE_G4   392.00f
#define NOTE_A4   440.00f
#define NOTE_B4   493.88f
#define NOTE_C5   523.25f
#define NOTE_D5   587.33f
#define NOTE_E5   659.25f
#define NOTE_F5   698.46f
#define NOTE_G5   783.99f
#define NOTE_A5   880.00f

// Giai điệu Super Mario Bros Theme (Mở đầu huyền thoại)
typedef struct Note {
    float freq;
    int durationMs;
} Note;

static const Note g_marioMelody[] = {
    { NOTE_E5, 120 }, { NOTE_E5, 120 }, { NOTE_REST, 120 }, { NOTE_E5, 120 },
    { NOTE_REST, 120 }, { NOTE_C5, 120 }, { NOTE_E5, 240 },
    { NOTE_G5, 240 }, { NOTE_REST, 240 }, { NOTE_G4, 240 }, { NOTE_REST, 240 },
    // Câu 2:
    { NOTE_C5, 200 }, { NOTE_REST, 100 }, { NOTE_G4, 200 }, { NOTE_REST, 100 },
    { NOTE_E4, 200 }, { NOTE_REST, 100 }, { NOTE_A4, 180 }, { NOTE_B4, 180 },
    { NOTE_REST, 60 }, { NOTE_A4, 180 }, { NOTE_REST, 60 }, { NOTE_G4, 240 },
    { NOTE_E5, 180 }, { NOTE_G5, 180 }, { NOTE_A5, 240 }, { NOTE_F5, 120 }, { NOTE_G5, 120 },
    { NOTE_REST, 120 }, { NOTE_E5, 180 }, { NOTE_C5, 120 }, { NOTE_D5, 120 }, { NOTE_B4, 240 },
    { NOTE_REST, 400 }
};
#define MELODY_LENGTH (sizeof(g_marioMelody) / sizeof(Note))

#define SAMPLE_RATE 44100
#define CHUNK_SAMPLES 4410 // 0.1s audio chunk per buffer
static int16_t g_pcmBufferA[CHUNK_SAMPLES * 2];
static int16_t g_pcmBufferB[CHUNK_SAMPLES * 2];

static volatile uint32_t g_currentNoteIndex = 0;
static volatile uint32_t g_noteSampleProgress = 0;
static volatile uint32_t g_chunksPlayed = 0;

// Hàm tổng hợp âm thanh Chiptune 8-bit (Square wave with duty cycle & envelope)
static void synthesizeNextAudioChunk(int16_t* buffer, uint32_t numSamples) {
    for (uint32_t i = 0; i < numSamples; ++i) {
        if (g_currentNoteIndex >= MELODY_LENGTH) {
            buffer[i * 2] = 0;
            buffer[i * 2 + 1] = 0;
            continue;
        }

        const Note* curNote = &g_marioMelody[g_currentNoteIndex];
        uint32_t totalNoteSamples = (curNote->durationMs * SAMPLE_RATE) / 1000;

        int16_t sampleVal = 0;
        if (curNote->freq > 0.0f) {
            // Chu kỳ sóng
            float period = (float)SAMPLE_RATE / curNote->freq;
            float posInPeriod = (float)(g_noteSampleProgress % (uint32_t)period);
            
            // 8-bit Square wave 50% duty cycle
            float rawWave = (posInPeriod < period * 0.5f) ? 1.0f : -1.0f;

            // Decay Envelope (giảm dần âm lượng cuối nốt tạo độ nảy)
            float envelope = 1.0f - ((float)g_noteSampleProgress / (float)totalNoteSamples) * 0.4f;
            sampleVal = (int16_t)(rawWave * 16000.0f * envelope);
        }

        buffer[i * 2] = sampleVal;     // Kênh Trái
        buffer[i * 2 + 1] = sampleVal; // Kênh Phải

        g_noteSampleProgress++;
        if (g_noteSampleProgress >= totalNoteSamples) {
            g_noteSampleProgress = 0;
            g_currentNoteIndex++;
        }
    }
}

// OpenSL Buffer Queue Streaming Callback
static void marioBufferCallback(SLAndroidSimpleBufferQueueItf bq, void* context) {
    (void)context;
    g_chunksPlayed++;

    if (g_currentNoteIndex < MELODY_LENGTH) {
        int16_t* nextBuf = (g_chunksPlayed % 2 == 0) ? g_pcmBufferA : g_pcmBufferB;
        synthesizeNextAudioChunk(nextBuf, CHUNK_SAMPLES);
        slAndroidSimpleBufferQueueEnqueue(bq, nextBuf, sizeof(g_pcmBufferA));
    }
}

int kudroid_test_main(void) {
    LOGI("=================================================");
    LOGI("🍄 [Super Mario Bros 8-bit BGM OpenSL Streaming]");
    LOGI("=================================================");

    g_currentNoteIndex = 0;
    g_noteSampleProgress = 0;
    g_chunksPlayed = 0;

    // 1. Khởi tạo Engine
    SLObjectItf engineObj = NULL;
    slCreateEngine(&engineObj, 0, NULL, 0, NULL, NULL);
    slObjectRealize(engineObj, 0);

    SLEngineItf engine = NULL;
    slObjectGetInterface(engineObj, (SLInterfaceID)1, &engine);

    SLObjectItf outputMix = NULL;
    slEngineCreateOutputMix(engine, &outputMix, 0, NULL, NULL);
    slObjectRealize(outputMix, 0);

    // 2. Tạo Audio Player với BufferQueue
    SLDataLocator_AndroidSimpleBufferQueue loc_bufq = { 1, 2 };
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
    slAndroidSimpleBufferQueueRegisterCallback(playerBufferQueue, (void*)marioBufferCallback, NULL);

    LOGI("🎶 Starting Super Mario Bros Theme Playback on iPhone Speaker...");
    slPlaySetPlayState(playerPlay, SL_PLAYSTATE_PLAYING);

    // Nạp trước 2 buffer đầu tiên (Double Buffering)
    synthesizeNextAudioChunk(g_pcmBufferA, CHUNK_SAMPLES);
    slAndroidSimpleBufferQueueEnqueue(playerBufferQueue, g_pcmBufferA, sizeof(g_pcmBufferA));

    synthesizeNextAudioChunk(g_pcmBufferB, CHUNK_SAMPLES);
    slAndroidSimpleBufferQueueEnqueue(playerBufferQueue, g_pcmBufferB, sizeof(g_pcmBufferB));

    // Chờ giai điệu Mario phát hết (khoảng 4.2 giây)
    while (g_currentNoteIndex < MELODY_LENGTH) {
        usleep(100000); // 100ms poll
    }
    // Chờ nốt cuối ngân xong
    usleep(500000);

    LOGI("✔ Mario Theme Playback finished successfully! (Chunks streamed: %u)", g_chunksPlayed);

    // Dọn dẹp
    slPlaySetPlayState(playerPlay, SL_PLAYSTATE_STOPPED);
    slObjectDestroy(playerObj);
    slObjectDestroy(outputMix);
    slObjectDestroy(engineObj);

    LOGI("=================================================");
    LOGI("🎉 MARIO THEME 8-BIT BGM STREAMING COMPLETED 100%!");
    LOGI("=================================================");
    return 0;
}
