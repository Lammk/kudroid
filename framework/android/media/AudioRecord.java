package android.media;

import java.nio.ByteBuffer;

public class AudioRecord {
    public static final int STATE_UNINITIALIZED = 0;
    public static final int STATE_INITIALIZED = 1;
    public static final int RECORDSTATE_STOPPED = 1;
    public static final int RECORDSTATE_RECORDING = 3;
    public static final int ERROR = -1;
    public static final int ERROR_BAD_VALUE = -2;
    public static final int ERROR_INVALID_OPERATION = -3;

    private int mState = STATE_INITIALIZED;
    private int mRecordingState = RECORDSTATE_STOPPED;

    public AudioRecord(int audioSource, int sampleRateInHz, int channelConfig, int audioFormat, int bufferSizeInBytes) {}
    public static int getMinBufferSize(int sampleRateInHz, int channelConfig, int audioFormat) { return 4096; }
    public void startRecording() { mRecordingState = RECORDSTATE_RECORDING; }
    public void stop() { mRecordingState = RECORDSTATE_STOPPED; }
    public int read(byte[] audioData, int offsetInBytes, int sizeInBytes) { return sizeInBytes; }
    public int read(short[] audioData, int offsetInShorts, int sizeInShorts) { return sizeInShorts; }
    public int read(ByteBuffer audioBuffer, int sizeInBytes) { return sizeInBytes; }
    public int getState() { return mState; }
    public int getRecordingState() { return mRecordingState; }
    public void release() { mState = STATE_UNINITIALIZED; }
}
