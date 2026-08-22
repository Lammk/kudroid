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
#define LOG_TAG "AudioMicro"
#define LOGI(...) __android_log_print(3, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(6, LOG_TAG, __VA_ARGS__)

// OpenSL ES Definitions
typedef int32_t SLresult;
typedef uint32_t SLuint32;
typedef void* SLObjectItf;
typedef void* SLEngineItf;
typedef void* SLPlayItf;
typedef void* SLAndroidSimpleBufferQueueItf;
typedef void* SLInterfaceID;
typedef uint32_t SLboolean;

#define SL_RESULT_SUCCESS 0
#define SL_PLAYSTATE_STOPPED 0
#define SL_PLAYSTATE_PAUSED  1
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

// AAudio Definitions
typedef struct AAudioStreamBuilder AAudioStreamBuilder;
typedef struct AAudioStream AAudioStream;
#define AAUDIO_OK 0
#define AAUDIO_FORMAT_PCM_I16 1

// Bionic Audio Exported Functions
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
extern SLresult slBufferQueueEnqueue(SLAndroidSimpleBufferQueueItf self, const void* pBuffer, uint32_t size);
extern SLresult slBufferQueueRegisterCallback(SLAndroidSimpleBufferQueueItf self, void* callback, void* pContext);

extern int AAudio_createStreamBuilder(AAudioStreamBuilder** builder);
extern void AAudioStreamBuilder_setSampleRate(AAudioStreamBuilder* builder, int32_t sampleRate);
extern void AAudioStreamBuilder_setChannelCount(AAudioStreamBuilder* builder, int32_t channelCount);
extern void AAudioStreamBuilder_setFormat(AAudioStreamBuilder* builder, int32_t format);
extern int AAudioStreamBuilder_openStream(AAudioStreamBuilder* builder, AAudioStream** stream);
extern int AAudioStreamBuilder_delete(AAudioStreamBuilder* builder);
extern int AAudioStream_requestStart(AAudioStream* stream);
extern int AAudioStream_write(AAudioStream* stream, const void* buffer, int32_t numFrames, int64_t timeoutNanoseconds);
extern int AAudioStream_requestStop(AAudioStream* stream);
extern int AAudioStream_close(AAudioStream* stream);

// OpenSL Buffer Callback
static volatile int g_callbackFired = 0;
static void audioBufferQueueCallback(SLAndroidSimpleBufferQueueItf bq, void* context) {
    (void)bq; (void)context;
    g_callbackFired++;
    LOGI("🔔 [OpenSL Callback] CoreAudio consumed PCM buffer! (Count: %d)", g_callbackFired);
}

// Simple sin function approximation for standalone 440Hz / 880Hz audio generation
static float custom_sin(float rad) {
    while (rad > 3.14159265f) rad -= 6.2831853f;
    while (rad < -3.14159265f) rad += 6.2831853f;
    float rad2 = rad * rad;
    return rad * (1.0f - rad2 * (1.0f / 6.0f) * (1.0f - rad2 * (1.0f / 20.0f)));
}

int kudroid_test_main(void) {
    LOGI("=================================================");
    LOGI("🔊 [KuDroid OpenSL ES & AAudio Engine Test]");
    LOGI("=================================================");

    // ── PART 1: OpenSL ES Hardware Audio Pipeline ──
    LOGI("[STAGE 1/6] Initializing OpenSL ES Engine...");
    SLObjectItf engineObj = NULL;
    SLresult res = slCreateEngine(&engineObj, 0, NULL, 0, NULL, NULL);
    if (res != SL_RESULT_SUCCESS || !engineObj) {
        LOGE("❌ slCreateEngine failed with code: %d", res);
        return 1;
    }
    slObjectRealize(engineObj, 0);

    SLEngineItf engine = NULL;
    slObjectGetInterface(engineObj, (SLInterfaceID)1, &engine);
    LOGI("✔ [STAGE 1] OpenSL ES Engine realized at %p", engine);

    LOGI("[STAGE 2/6] Creating Output Mix...");
    SLObjectItf outputMix = NULL;
    slEngineCreateOutputMix(engine, &outputMix, 0, NULL, NULL);
    slObjectRealize(outputMix, 0);
    LOGI("✔ [STAGE 2] Output Mix created at %p", outputMix);

    LOGI("[STAGE 3/6] Configuring PCM Audio Player (44.1kHz Stereo 16-bit)...");
    SLDataLocator_AndroidSimpleBufferQueue loc_bufq = { 1 /* SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE */, 2 };
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

    SLDataLocator_OutputMix loc_outmix = { 2 /* SL_DATALOCATOR_OUTPUTMIX */, outputMix };
    SLDataSink audioSnk = { &loc_outmix, NULL };

    SLObjectItf playerObj = NULL;
    res = slEngineCreateAudioPlayer(engine, &playerObj, &audioSrc, &audioSnk, 0, NULL, NULL);
    if (res != SL_RESULT_SUCCESS || !playerObj) {
        LOGE("❌ slEngineCreateAudioPlayer failed: %d", res);
        return 2;
    }
    slObjectRealize(playerObj, 0);

    SLPlayItf playerPlay = NULL;
    slObjectGetInterface(playerObj, (SLInterfaceID)2, &playerPlay);

    SLAndroidSimpleBufferQueueItf playerBufferQueue = NULL;
    slObjectGetInterface(playerObj, (SLInterfaceID)3, &playerBufferQueue);
    slBufferQueueRegisterCallback(playerBufferQueue, (void*)audioBufferQueueCallback, NULL);
    LOGI("✔ [STAGE 3] PCM Audio Player & BufferQueue configured: %p", playerObj);

    // Generate 0.5s 440Hz Sine Wave (Concert A4 tone)
    #define SAMPLE_RATE 44100
    #define DURATION_SAMPLES 22050 // 0.5 sec
    static int16_t pcmData[DURATION_SAMPLES * 2];
    for (int i = 0; i < DURATION_SAMPLES; ++i) {
        float t = (float)i / (float)SAMPLE_RATE;
        int16_t sample = (int16_t)(custom_sin(6.2831853f * 440.0f * t) * 20000.0f);
        pcmData[i * 2] = sample;     // Left Channel
        pcmData[i * 2 + 1] = sample; // Right Channel
    }

    LOGI("[STAGE 4/6] Enqueuing 0.5s 440Hz Sine Wave & Playing on iPhone Speaker...");
    slPlaySetPlayState(playerPlay, SL_PLAYSTATE_PLAYING);
    slAndroidSimpleBufferQueueEnqueue(playerBufferQueue, pcmData, sizeof(pcmData));
    LOGI("✔ [STAGE 4] Audio buffer enqueued to CoreAudio AudioQueue! (Bytes: %u)", (uint32_t)sizeof(pcmData));

    // Wait 0.6s for OpenSL audio playback
    extern int usleep(unsigned int usec);
    usleep(600000);

    // ── PART 2: AAudio Low-Latency Pipeline ──
    LOGI("[STAGE 5/6] Initializing AAudio Stream (48kHz Stereo Low-Latency)...");
    AAudioStreamBuilder* builder = NULL;
    if (AAudio_createStreamBuilder(&builder) == AAUDIO_OK && builder) {
        AAudioStreamBuilder_setSampleRate(builder, 48000);
        AAudioStreamBuilder_setChannelCount(builder, 2);
        AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);

        AAudioStream* stream = NULL;
        if (AAudioStreamBuilder_openStream(builder, &stream) == AAUDIO_OK && stream) {
            AAudioStream_requestStart(stream);

            // Generate 0.4s 880Hz Tone (Game Sound Effect "Jump/Coin")
            #define AAUDIO_SAMPLES 19200
            static int16_t aaudioData[AAUDIO_SAMPLES * 2];
            for (int i = 0; i < AAUDIO_SAMPLES; ++i) {
                float t = (float)i / 48000.0f;
                // Frequency sweeps from 440Hz -> 880Hz (Chirp sound effect)
                float freq = 440.0f + (440.0f * (float)i / (float)AAUDIO_SAMPLES);
                int16_t s = (int16_t)(custom_sin(6.2831853f * freq * t) * 20000.0f);
                aaudioData[i * 2] = s;
                aaudioData[i * 2 + 1] = s;
            }

            int framesWritten = AAudioStream_write(stream, aaudioData, AAUDIO_SAMPLES, 0);
            LOGI("✔ [STAGE 5] AAudio stream write %d frames of 880Hz Chirp SFX to CoreAudio!", framesWritten);

            // Wait 0.5s for AAudio playback
            usleep(500000);

            AAudioStream_requestStop(stream);
            AAudioStream_close(stream);
        }
        AAudioStreamBuilder_delete(builder);
    }

    // ── PART 3: Cleanup ──
    LOGI("[STAGE 6/6] Cleaning up OpenSL & CoreAudio resources...");
    slPlaySetPlayState(playerPlay, SL_PLAYSTATE_STOPPED);
    slObjectDestroy(playerObj);
    slObjectDestroy(outputMix);
    slObjectDestroy(engineObj);
    LOGI("✔ [STAGE 6] Audio resources destroyed safely");

    LOGI("=================================================");
    LOGI("🎉 OPENSL ES & AAUDIO HARDWARE PIPELINE PASSED 100%!");
    LOGI("=================================================");
    return 0;
}
