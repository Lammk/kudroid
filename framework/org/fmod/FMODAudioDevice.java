package org.fmod;

import android.media.AudioFormat;
import android.media.AudioManager;
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

    public native int fmodGetInfo(int which);
    public native int fmodProcess(ByteBuffer buffer);

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
