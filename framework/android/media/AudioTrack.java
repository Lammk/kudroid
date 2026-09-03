package android.media;

import java.nio.ByteBuffer;

/**
 * Real PCM output, backed by the host's audio queue.
 *
 * Why this is not a stub. FMOD — which is what Unity games ship for audio — drives
 * AudioTrack directly from Java: getMinBufferSize, construct, getState, play, write.
 * The generated stub had a single empty constructor, so every one of those was
 * auto-stubbed to return 0, and FMOD read that as "buffer size 0, state uninitialised":
 *
 *     [KuART][MISSING-METHOD] Auto-stubbing: AudioTrack->getMinBufferSize(III)I
 *     [E/FMOD] AudioTrack failed to initialize (status 0)
 *
 * It then retried forever. The thread sampler caught the loop: pc moving inside
 * libsystem_malloc with lr in DexClassLinker::FindClass, five samples ten seconds apart,
 * CPU climbing — a spin, not a park. ULTRAKILL never reached its first frame.
 *
 * getState() is the specific check that matters: FMOD compares it against
 * STATE_INITIALIZED, so returning 0 is a permanent failure rather than a degraded mode,
 * and no amount of retrying can get past it.
 *
 * The native methods below reach AudioShim, which already owns a real CoreAudio
 * AudioQueue on iOS — the same path OpenSL ES and AAudio use. Sound is genuinely
 * produced; this is not a silence-shaped stub that merely lets FMOD continue.
 */
public class AudioTrack {

    public static final int MODE_STATIC = 0;
    public static final int MODE_STREAM = 1;

    public static final int STATE_UNINITIALIZED = 0;
    public static final int STATE_INITIALIZED = 1;
    public static final int STATE_NO_STATIC_DATA = 2;

    public static final int PLAYSTATE_STOPPED = 1;
    public static final int PLAYSTATE_PAUSED = 2;
    public static final int PLAYSTATE_PLAYING = 3;

    public static final int SUCCESS = 0;
    public static final int ERROR = -1;
    public static final int ERROR_BAD_VALUE = -2;
    public static final int ERROR_INVALID_OPERATION = -3;
    public static final int ERROR_DEAD_OBJECT = -6;

    public static final int WRITE_BLOCKING = 0;
    public static final int WRITE_NON_BLOCKING = 1;

    public static final int PERFORMANCE_MODE_NONE = 0;
    public static final int PERFORMANCE_MODE_LOW_LATENCY = 1;
    public static final int PERFORMANCE_MODE_POWER_SAVING = 2;

    /** Handle into AudioShim's player table. 0 means construction failed. */
    private long nativeTrack;

    private int mState = STATE_UNINITIALIZED;
    private int mPlayState = PLAYSTATE_STOPPED;
    private final int mSampleRate;
    private final int mChannelCount;
    private final int mAudioFormat;
    private final int mBufferSizeInBytes;
    private float mVolume = 1.0f;

    // ── native bridge (implemented in src/kuart/LibCore.cpp) ─────────────────

    private static native int nativeGetMinBufferSize(int sampleRateInHz, int channelCount,
                                                     int audioFormat);
    private static native long nativeCreate(int sampleRateInHz, int channelCount,
                                            int audioFormat, int bufferSizeInBytes);
    private static native int nativeWrite(long track, byte[] data, int offsetInBytes,
                                          int sizeInBytes);
    private static native int nativeWriteShorts(long track, short[] data, int offsetInShorts,
                                                int sizeInShorts);
    private static native int nativePlay(long track);
    private static native int nativePause(long track);
    private static native int nativeStop(long track);
    private static native int nativeFlush(long track);
    private static native int nativeRelease(long track);
    private static native int nativeSetVolume(long track, float volume);
    private static native int nativeGetPlaybackHeadPosition(long track);
    private static native int nativeGetLatencyFrames(long track);

    // ── channel configuration ────────────────────────────────────────────────

    // AudioFormat.CHANNEL_OUT_* is a bit mask, and the count is how many bits are set.
    // FMOD passes CHANNEL_OUT_STEREO (0xC) and Unity's own code sometimes passes the
    // deprecated CHANNEL_CONFIGURATION_* values, which have the same meaning.
    private static int channelCountFromConfig(int channelConfig) {
        switch (channelConfig) {
            case AudioFormat.CHANNEL_OUT_MONO:   // 0x4
                return 1;
            case AudioFormat.CHANNEL_OUT_STEREO: // 0xC
                return 2;
            default:
                break;
        }
        // A mask we do not recognise: count the bits rather than guessing, which keeps
        // 5.1/7.1 layouts approximately right instead of silently becoming mono.
        final int bits = Integer.bitCount(channelConfig);
        return bits > 0 ? bits : 2;
    }

    private static int bytesPerSample(int audioFormat) {
        switch (audioFormat) {
            case AudioFormat.ENCODING_PCM_8BIT:  return 1;
            case AudioFormat.ENCODING_PCM_FLOAT: return 4;
            case AudioFormat.ENCODING_PCM_16BIT:
            default:                             return 2;
        }
    }

    // ── construction ─────────────────────────────────────────────────────────

    /**
     * The minimum buffer size for these parameters, in bytes.
     *
     * Returning 0 or a negative value here is fatal to FMOD, which treats it as
     * ERROR_BAD_VALUE and gives up on the device. The host decides the real figure —
     * AudioShim knows the queue's period — and this only falls back to a conservative
     * value if the host has nothing to say.
     */
    public static int getMinBufferSize(int sampleRateInHz, int channelConfig, int audioFormat) {
        final int channels = channelCountFromConfig(channelConfig);
        final int fromHost = nativeGetMinBufferSize(sampleRateInHz, channels, audioFormat);
        if (fromHost > 0) return fromHost;
        // Roughly 20ms, rounded to a frame boundary. Chosen over ERROR_BAD_VALUE because
        // a caller that gets an error stops, while a caller that gets a workable size
        // plays audio.
        final int frames = Math.max(sampleRateInHz, 8000) / 50;
        return frames * channels * bytesPerSample(audioFormat);
    }

    public AudioTrack(int streamType, int sampleRateInHz, int channelConfig, int audioFormat,
                      int bufferSizeInBytes, int mode) {
        this(streamType, sampleRateInHz, channelConfig, audioFormat, bufferSizeInBytes, mode, 0);
    }

    public AudioTrack(int streamType, int sampleRateInHz, int channelConfig, int audioFormat,
                      int bufferSizeInBytes, int mode, int sessionId) {
        mSampleRate = sampleRateInHz > 0 ? sampleRateInHz : 44100;
        mChannelCount = channelCountFromConfig(channelConfig);
        mAudioFormat = audioFormat;
        mBufferSizeInBytes = bufferSizeInBytes > 0
                ? bufferSizeInBytes
                : getMinBufferSize(mSampleRate, channelConfig, audioFormat);

        nativeTrack = nativeCreate(mSampleRate, mChannelCount, mAudioFormat, mBufferSizeInBytes);
        // STATE_INITIALIZED is what FMOD checks, and it is only claimed when the host
        // really gave us a player. Claiming it unconditionally would move the failure
        // from a clear "AudioTrack failed to initialize" to silence with no explanation.
        mState = nativeTrack != 0 ? STATE_INITIALIZED : STATE_UNINITIALIZED;
    }

    public AudioTrack(AudioAttributes attributes, AudioFormat format, int bufferSizeInBytes,
                      int mode, int sessionId) {
        mSampleRate = format != null && format.getSampleRate() > 0 ? format.getSampleRate() : 44100;
        mChannelCount = format != null ? Math.max(1, format.getChannelCount()) : 2;
        mAudioFormat = format != null ? format.getEncoding() : AudioFormat.ENCODING_PCM_16BIT;
        mBufferSizeInBytes = bufferSizeInBytes > 0
                ? bufferSizeInBytes
                : (mSampleRate / 50) * mChannelCount * bytesPerSample(mAudioFormat);

        nativeTrack = nativeCreate(mSampleRate, mChannelCount, mAudioFormat, mBufferSizeInBytes);
        mState = nativeTrack != 0 ? STATE_INITIALIZED : STATE_UNINITIALIZED;
    }

    // ── state ────────────────────────────────────────────────────────────────

    public int getState() { return mState; }
    public int getPlayState() { return mPlayState; }
    public int getSampleRate() { return mSampleRate; }
    public int getPlaybackRate() { return mSampleRate; }
    public int getChannelCount() { return mChannelCount; }
    public int getAudioFormat() { return mAudioFormat; }
    public int getBufferSizeInFrames() {
        final int frameBytes = mChannelCount * bytesPerSample(mAudioFormat);
        return frameBytes > 0 ? mBufferSizeInBytes / frameBytes : 0;
    }
    public int getAudioSessionId() { return 0; }
    public int getStreamType() { return AudioManager.STREAM_MUSIC; }

    public int getChannelConfiguration() {
        return mChannelCount == 1 ? AudioFormat.CHANNEL_OUT_MONO : AudioFormat.CHANNEL_OUT_STEREO;
    }

    /**
     * Frames consumed by the device so far.
     *
     * FMOD polls this to decide how much more to write, so a value frozen at 0 makes it
     * believe the device never drains and it stops writing. The host counts real frames.
     */
    public int getPlaybackHeadPosition() {
        return nativeTrack != 0 ? nativeGetPlaybackHeadPosition(nativeTrack) : 0;
    }

    public static float getMaxVolume() { return 1.0f; }
    public static float getMinVolume() { return 0.0f; }

    public static int getNativeOutputSampleRate(int streamType) {
        final int rate = nativeGetMinBufferSize(0, 0, -1);  // -1 asks for the device rate
        return rate > 0 ? rate : 48000;
    }

    // ── transport ────────────────────────────────────────────────────────────

    public void play() throws IllegalStateException {
        if (mState != STATE_INITIALIZED) {
            throw new IllegalStateException("play() called on uninitialized AudioTrack");
        }
        nativePlay(nativeTrack);
        mPlayState = PLAYSTATE_PLAYING;
    }

    public void pause() throws IllegalStateException {
        if (mState != STATE_INITIALIZED) {
            throw new IllegalStateException("pause() called on uninitialized AudioTrack");
        }
        nativePause(nativeTrack);
        mPlayState = PLAYSTATE_PAUSED;
    }

    public void stop() throws IllegalStateException {
        if (mState != STATE_INITIALIZED) {
            throw new IllegalStateException("stop() called on uninitialized AudioTrack");
        }
        nativeStop(nativeTrack);
        mPlayState = PLAYSTATE_STOPPED;
    }

    public void flush() {
        if (nativeTrack != 0) nativeFlush(nativeTrack);
    }

    public void release() {
        if (nativeTrack != 0) {
            nativeRelease(nativeTrack);
            nativeTrack = 0;
        }
        mState = STATE_UNINITIALIZED;
        mPlayState = PLAYSTATE_STOPPED;
    }

    // ── writing ──────────────────────────────────────────────────────────────

    public int write(byte[] audioData, int offsetInBytes, int sizeInBytes) {
        if (mState != STATE_INITIALIZED || audioData == null) return ERROR_INVALID_OPERATION;
        if (offsetInBytes < 0 || sizeInBytes < 0 ||
            offsetInBytes + sizeInBytes > audioData.length) {
            return ERROR_BAD_VALUE;
        }
        return nativeWrite(nativeTrack, audioData, offsetInBytes, sizeInBytes);
    }

    public int write(byte[] audioData, int offsetInBytes, int sizeInBytes, int writeMode) {
        return write(audioData, offsetInBytes, sizeInBytes);
    }

    public int write(short[] audioData, int offsetInShorts, int sizeInShorts) {
        if (mState != STATE_INITIALIZED || audioData == null) return ERROR_INVALID_OPERATION;
        if (offsetInShorts < 0 || sizeInShorts < 0 ||
            offsetInShorts + sizeInShorts > audioData.length) {
            return ERROR_BAD_VALUE;
        }
        return nativeWriteShorts(nativeTrack, audioData, offsetInShorts, sizeInShorts);
    }

    public int write(short[] audioData, int offsetInShorts, int sizeInShorts, int writeMode) {
        return write(audioData, offsetInShorts, sizeInShorts);
    }

    public int write(ByteBuffer audioData, int sizeInBytes, int writeMode) {
        if (mState != STATE_INITIALIZED || audioData == null) return ERROR_INVALID_OPERATION;
        final int n = Math.min(sizeInBytes, audioData.remaining());
        if (n <= 0) return 0;
        final byte[] copy = new byte[n];
        audioData.get(copy, 0, n);
        return nativeWrite(nativeTrack, copy, 0, n);
    }

    // ── volume ───────────────────────────────────────────────────────────────

    public int setVolume(float gain) {
        mVolume = gain;
        if (nativeTrack == 0) return ERROR_INVALID_OPERATION;
        return nativeSetVolume(nativeTrack, gain);
    }

    /** Deprecated stereo form; the host applies a single gain. */
    public int setStereoVolume(float leftGain, float rightGain) {
        return setVolume(Math.max(leftGain, rightGain));
    }

    public float getVolume() { return mVolume; }

    public int setPlaybackRate(int sampleRateInHz) { return SUCCESS; }
    public int setNotificationMarkerPosition(int markerInFrames) { return SUCCESS; }
    public int setPositionNotificationPeriod(int periodInFrames) { return SUCCESS; }
    public int setLoopPoints(int startInFrames, int endInFrames, int loopCount) { return SUCCESS; }
    public int reloadStaticData() { return SUCCESS; }
    public int attachAuxEffect(int effectId) { return SUCCESS; }
    public int setAuxEffectSendLevel(float level) { return SUCCESS; }

    public int getPerformanceMode() { return PERFORMANCE_MODE_LOW_LATENCY; }
    public int getUnderrunCount() { return 0; }

    /** Round-trip latency in frames, as the host measures it. */
    public int getLatencyFrames() {
        return nativeTrack != 0 ? nativeGetLatencyFrames(nativeTrack) : 0;
    }

    // ── builder, for code that uses the modern API ───────────────────────────

    public static class Builder {
        private AudioFormat mFormat;
        private AudioAttributes mAttributes;
        private int mBufferSize = 0;
        private int mMode = MODE_STREAM;
        private int mSessionId = 0;

        public Builder() {}
        public Builder setAudioFormat(AudioFormat format) { mFormat = format; return this; }
        public Builder setAudioAttributes(AudioAttributes attributes) {
            mAttributes = attributes;
            return this;
        }
        public Builder setBufferSizeInBytes(int bufferSizeInBytes) {
            mBufferSize = bufferSizeInBytes;
            return this;
        }
        public Builder setTransferMode(int mode) { mMode = mode; return this; }
        public Builder setSessionId(int sessionId) { mSessionId = sessionId; return this; }
        public Builder setPerformanceMode(int performanceMode) { return this; }

        public AudioTrack build() {
            return new AudioTrack(mAttributes, mFormat, mBufferSize, mMode, mSessionId);
        }
    }
}
