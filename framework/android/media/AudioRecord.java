package android.media;

import java.nio.ByteBuffer;

/**
 * Real microphone capture, backed by the host's audio input.
 *
 * Needs RECORD_AUDIO: without it the object stays STATE_UNINITIALIZED, as on Android,
 * and startRecording() throws.
 */
public class AudioRecord {
    public static final int STATE_UNINITIALIZED = 0;
    public static final int STATE_INITIALIZED = 1;
    public static final int RECORDSTATE_STOPPED = 1;
    public static final int RECORDSTATE_RECORDING = 3;
    public static final int SUCCESS = 0;
    public static final int ERROR = -1;
    public static final int ERROR_BAD_VALUE = -2;
    public static final int ERROR_INVALID_OPERATION = -3;
    public static final int ERROR_DEAD_OBJECT = -6;
    public static final int READ_BLOCKING = 0;
    public static final int READ_NON_BLOCKING = 1;

    /** Handle into the host's recorder table. 0 means construction failed. */
    private long mNativeRecorder;
    private int mState = STATE_UNINITIALIZED;
    private int mRecordingState = RECORDSTATE_STOPPED;
    private final int mAudioSource;
    private final int mSampleRate;
    private final int mChannelCount;
    private final int mAudioFormat;
    private final int mBufferSizeInBytes;

    // Reused across reads: the heap has no collector, so a fresh array per read would leak.
    private short[] mScratch = new short[0];

    private static native int nativeGetMinBufferSize(int sampleRateInHz, int channelCount);
    private static native long nativeCreate(int sampleRateInHz, int channelCount,
                                            int bufferSizeInBytes);
    private static native int nativeStart(long recorder);
    private static native int nativeStop(long recorder);
    private static native int nativeRead(long recorder, byte[] data, int offsetInBytes,
                                         int sizeInBytes, boolean blocking);
    private static native int nativeReadShorts(long recorder, short[] data, int offsetInShorts,
                                               int sizeInShorts, boolean blocking);
    private static native int nativeRelease(long recorder);

    private static int channelCountFromConfig(int channelConfig) {
        return Math.max(1, Integer.bitCount(channelConfig));
    }

    private static boolean supportedEncoding(int audioFormat) {
        return audioFormat == AudioFormat.ENCODING_PCM_16BIT
                || audioFormat == AudioFormat.ENCODING_PCM_8BIT
                || audioFormat == AudioFormat.ENCODING_PCM_FLOAT;
    }

    private static int bytesPerSample(int audioFormat) {
        switch (audioFormat) {
            case AudioFormat.ENCODING_PCM_8BIT:  return 1;
            case AudioFormat.ENCODING_PCM_FLOAT: return 4;
            default:                             return 2;
        }
    }

    public static int getMinBufferSize(int sampleRateInHz, int channelConfig, int audioFormat) {
        if (!supportedEncoding(audioFormat)) return ERROR_BAD_VALUE;
        final int channels = channelCountFromConfig(channelConfig);
        final int bytes16 = nativeGetMinBufferSize(sampleRateInHz, channels);
        if (bytes16 < 0) return bytes16;
        return bytes16 / 2 * bytesPerSample(audioFormat);
    }

    public AudioRecord(int audioSource, int sampleRateInHz, int channelConfig, int audioFormat,
                       int bufferSizeInBytes) {
        mAudioSource = audioSource;
        mSampleRate = sampleRateInHz;
        mChannelCount = channelCountFromConfig(channelConfig);
        mAudioFormat = audioFormat;
        final int min = getMinBufferSize(sampleRateInHz, channelConfig, audioFormat);
        mBufferSizeInBytes = Math.max(bufferSizeInBytes, min);
        if (min < 0 || !supportedEncoding(audioFormat)) return;
        if (!android.content.pm.RuntimePermissions.isGranted(
                android.app.ActivityThread.getPackageName(),
                android.content.pm.RuntimePermissions.RECORD_AUDIO)) {
            android.util.Log.w("AudioRecord", "RECORD_AUDIO is not granted");
            return;
        }
        // The host always captures 16-bit; wider encodings are converted on read.
        final int bytes16 = mBufferSizeInBytes / bytesPerSample(audioFormat) * 2;
        mNativeRecorder = nativeCreate(sampleRateInHz, mChannelCount, bytes16);
        mState = mNativeRecorder != 0 ? STATE_INITIALIZED : STATE_UNINITIALIZED;
    }

    public static class Builder {
        private int mSource = 0;
        private int mSampleRate = 44100;
        private int mChannelMask = AudioFormat.CHANNEL_IN_MONO;
        private int mEncoding = AudioFormat.ENCODING_PCM_16BIT;
        private int mBufferSize = 0;

        public Builder() {}
        public Builder setAudioSource(int source) { mSource = source; return this; }
        public Builder setAudioFormat(AudioFormat format) {
            mSampleRate = format.getSampleRate();
            mChannelMask = format.getChannelMask();
            mEncoding = format.getEncoding();
            return this;
        }
        public Builder setBufferSizeInBytes(int bytes) { mBufferSize = bytes; return this; }
        public AudioRecord build() {
            return new AudioRecord(mSource, mSampleRate, mChannelMask, mEncoding, mBufferSize);
        }
    }

    public void startRecording() {
        if (mState != STATE_INITIALIZED) {
            throw new IllegalStateException(
                    "startRecording() called on an uninitialized AudioRecord.");
        }
        if (mRecordingState == RECORDSTATE_RECORDING) return;
        if (nativeStart(mNativeRecorder) == SUCCESS) mRecordingState = RECORDSTATE_RECORDING;
    }

    public void stop() {
        if (mState != STATE_INITIALIZED) {
            throw new IllegalStateException("stop() called on an uninitialized AudioRecord.");
        }
        nativeStop(mNativeRecorder);
        mRecordingState = RECORDSTATE_STOPPED;
    }

    public void release() {
        if (mNativeRecorder != 0) nativeRelease(mNativeRecorder);
        mNativeRecorder = 0;
        mState = STATE_UNINITIALIZED;
        mRecordingState = RECORDSTATE_STOPPED;
    }

    private boolean readable() { return mState == STATE_INITIALIZED && mNativeRecorder != 0; }

    private short[] scratch(int shorts) {
        if (mScratch.length < shorts) mScratch = new short[shorts];
        return mScratch;
    }

    public int read(byte[] audioData, int offsetInBytes, int sizeInBytes) {
        return read(audioData, offsetInBytes, sizeInBytes, READ_BLOCKING);
    }

    public int read(byte[] audioData, int offsetInBytes, int sizeInBytes, int readMode) {
        if (!readable()) return ERROR_INVALID_OPERATION;
        if (audioData == null || offsetInBytes < 0 || sizeInBytes < 0
                || offsetInBytes + sizeInBytes > audioData.length) {
            return ERROR_BAD_VALUE;
        }
        final boolean blocking = readMode == READ_BLOCKING;
        if (mAudioFormat == AudioFormat.ENCODING_PCM_8BIT) {
            final short[] pcm = scratch(sizeInBytes);
            final int n = nativeReadShorts(mNativeRecorder, pcm, 0, sizeInBytes, blocking);
            if (n <= 0) return n;
            for (int i = 0; i < n; ++i) {
                audioData[offsetInBytes + i] = (byte) ((pcm[i] >> 8) + 128);
            }
            return n;
        }
        if (mAudioFormat == AudioFormat.ENCODING_PCM_FLOAT) return ERROR_INVALID_OPERATION;
        return nativeRead(mNativeRecorder, audioData, offsetInBytes, sizeInBytes, blocking);
    }

    public int read(short[] audioData, int offsetInShorts, int sizeInShorts) {
        return read(audioData, offsetInShorts, sizeInShorts, READ_BLOCKING);
    }

    public int read(short[] audioData, int offsetInShorts, int sizeInShorts, int readMode) {
        if (!readable()) return ERROR_INVALID_OPERATION;
        if (mAudioFormat != AudioFormat.ENCODING_PCM_16BIT) return ERROR_INVALID_OPERATION;
        if (audioData == null || offsetInShorts < 0 || sizeInShorts < 0
                || offsetInShorts + sizeInShorts > audioData.length) {
            return ERROR_BAD_VALUE;
        }
        return nativeReadShorts(mNativeRecorder, audioData, offsetInShorts, sizeInShorts,
                readMode == READ_BLOCKING);
    }

    public int read(float[] audioData, int offsetInFloats, int sizeInFloats, int readMode) {
        if (!readable()) return ERROR_INVALID_OPERATION;
        if (mAudioFormat != AudioFormat.ENCODING_PCM_FLOAT) return ERROR_INVALID_OPERATION;
        if (audioData == null || offsetInFloats < 0 || sizeInFloats < 0
                || offsetInFloats + sizeInFloats > audioData.length) {
            return ERROR_BAD_VALUE;
        }
        final short[] pcm = scratch(sizeInFloats);
        final int n = nativeReadShorts(mNativeRecorder, pcm, 0, sizeInFloats,
                readMode == READ_BLOCKING);
        if (n <= 0) return n;
        for (int i = 0; i < n; ++i) audioData[offsetInFloats + i] = pcm[i] / 32768f;
        return n;
    }

    public int read(ByteBuffer audioBuffer, int sizeInBytes) {
        return read(audioBuffer, sizeInBytes, READ_BLOCKING);
    }

    /** Fills the buffer from index 0 and leaves its position unchanged, as the platform does. */
    public int read(ByteBuffer audioBuffer, int sizeInBytes, int readMode) {
        if (!readable()) return ERROR_INVALID_OPERATION;
        if (audioBuffer == null || sizeInBytes < 0) return ERROR_BAD_VALUE;
        if (mAudioFormat != AudioFormat.ENCODING_PCM_16BIT) return ERROR_INVALID_OPERATION;
        final int size = Math.min(sizeInBytes, audioBuffer.capacity());
        final short[] pcm = scratch(size / 2);
        final int n = nativeReadShorts(mNativeRecorder, pcm, 0, size / 2,
                readMode == READ_BLOCKING);
        if (n <= 0) return n;
        for (int i = 0; i < n; ++i) {
            audioBuffer.put(2 * i, (byte) pcm[i]);
            audioBuffer.put(2 * i + 1, (byte) (pcm[i] >> 8));
        }
        return n * 2;
    }

    public int getState() { return mState; }
    public int getRecordingState() { return mRecordingState; }
    public int getAudioSource() { return mAudioSource; }
    public int getSampleRate() { return mSampleRate; }
    public int getChannelCount() { return mChannelCount; }
    public int getAudioFormat() { return mAudioFormat; }
    public int getBufferSizeInFrames() {
        return mBufferSizeInBytes / (mChannelCount * bytesPerSample(mAudioFormat));
    }
}
