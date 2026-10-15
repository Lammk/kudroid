package org.fmod;

import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioRecord;
import android.media.AudioTrack;
import java.nio.ByteBuffer;

/**
 * FMOD AudioDevice wrapper for Android.
 * Handles audio playback thread and JNI callbacks to FMOD C runtime.
 */
public class FMODAudioDevice implements Runnable {
    private volatile boolean mRunning = false;
    private Thread mThread;
    private AudioTrack mTrack;
    private AudioRecord mRecorder;
    private Thread mRecordThread;
    private volatile boolean mRecording;

    public native int fmodGetInfo(int which);
    public native int fmodProcess(ByteBuffer buffer);
    public native int fmodProcessMicData(ByteBuffer buffer, int length);

    public FMODAudioDevice() {}

    public synchronized void start() {
        if (mRunning) return;
        mRunning = true;
        mThread = new Thread(this, "FMODAudioDevice");
        mThread.setPriority(Thread.MAX_PRIORITY);
        mThread.start();
    }

    public synchronized void stop() {
        close();
    }

    public synchronized void close() {
        stopAudioRecord();
        if (!mRunning) return;
        mRunning = false;
        if (mThread != null) {
            try {
                mThread.join(500);
            } catch (InterruptedException ignored) {}
            mThread = null;
        }
        if (mTrack != null) {
            try {
                mTrack.stop();
                mTrack.release();
            } catch (Throwable ignored) {}
            mTrack = null;
        }
    }

    public boolean isRunning() {
        return mRunning;
    }

    /**
     * Start capturing the microphone and feed it to FMOD through fmodProcessMicData.
     * Returns the capture buffer size in bytes, or 0 when recording could not start
     * (no permission, or no input), which keeps output initialization unaffected.
     */
    public synchronized int startAudioRecord(int sampleRate, int channelConfig, int audioFormat) {
        if (mRecorder != null) return 0;
        final int minBuf = AudioRecord.getMinBufferSize(sampleRate, channelConfig, audioFormat);
        if (minBuf <= 0) return 0;
        final AudioRecord recorder;
        final ByteBuffer buffer;
        try {
            recorder = new AudioRecord(1, sampleRate, channelConfig, audioFormat, minBuf * 2);
            if (recorder.getState() != AudioRecord.STATE_INITIALIZED) {
                recorder.release();
                return 0;
            }
            buffer = ByteBuffer.allocateDirect(minBuf);
            recorder.startRecording();
        } catch (Throwable t) {
            return 0;
        }
        mRecorder = recorder;
        mRecording = true;
        mRecordThread = new Thread(new Runnable() {
            @Override public void run() {
                while (mRecording) {
                    final int n = recorder.read(buffer, minBuf);
                    if (n > 0) {
                        try {
                            fmodProcessMicData(buffer, n);
                        } catch (Throwable t) {
                            break;
                        }
                    } else if (n < 0) {
                        break;
                    }
                }
            }
        }, "FMODAudioRecord");
        mRecordThread.start();
        return minBuf;
    }

    public synchronized void stopAudioRecord() {
        mRecording = false;
        if (mRecorder != null) {
            try { mRecorder.stop(); } catch (Throwable ignored) {}
        }
        if (mRecordThread != null) {
            try { mRecordThread.join(500); } catch (InterruptedException ignored) {}
            mRecordThread = null;
        }
        if (mRecorder != null) {
            try { mRecorder.release(); } catch (Throwable ignored) {}
            mRecorder = null;
        }
    }

    @Override
    public void run() {
        int rate = 0;
        int frames = 0;
        for (int i = 0; mRunning && i < 500; i++) {
            try {
                rate = fmodGetInfo(0);
                frames = fmodGetInfo(1);
            } catch (Throwable t) {
                break;
            }
            if (rate > 0 && frames > 0) break;
            try {
                Thread.sleep(10);
            } catch (InterruptedException ignored) {
                break;
            }
        }

        if (rate <= 0 || frames <= 0 || frames > 65536 || !mRunning) {
            mRunning = false;
            return;
        }

        int bufferBytes = frames * 2 * 2; // 16-bit stereo: frames * 2 channels * 2 bytes
        ByteBuffer byteBuffer;
        try {
            byteBuffer = ByteBuffer.allocateDirect(bufferBytes);
        } catch (Throwable t) {
            mRunning = false;
            return;
        }

        int channelConfig = AudioFormat.CHANNEL_OUT_STEREO;
        int minBuf = AudioTrack.getMinBufferSize(rate, channelConfig, AudioFormat.ENCODING_PCM_16BIT);
        int trackBufSize = Math.max(bufferBytes * 2, minBuf);

        try {
            mTrack = new AudioTrack(AudioManager.STREAM_MUSIC, rate, channelConfig,
                                    AudioFormat.ENCODING_PCM_16BIT, trackBufSize, AudioTrack.MODE_STREAM);
            if (mTrack.getState() != AudioTrack.STATE_INITIALIZED) {
                mRunning = false;
                return;
            }
            mTrack.play();
        } catch (Throwable t) {
            mRunning = false;
            return;
        }

        byte[] transfer = new byte[bufferBytes];
        while (mRunning) {
            byteBuffer.clear();
            int r;
            try {
                r = fmodProcess(byteBuffer);
            } catch (Throwable t) {
                break;
            }
            if (r == 0) { // FMOD_OK
                byteBuffer.position(0);
                byteBuffer.get(transfer, 0, bufferBytes);
                if (mTrack != null) {
                    mTrack.write(transfer, 0, bufferBytes);
                }
            } else {
                try {
                    Thread.sleep(2);
                } catch (InterruptedException ignored) {
                    break;
                }
            }
        }

        if (mTrack != null) {
            try {
                mTrack.stop();
                mTrack.release();
            } catch (Throwable ignored) {}
            mTrack = null;
        }
    }
}
