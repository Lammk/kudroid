package org.fmod;

import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioTrack;

public class AudioDevice {
    private AudioTrack mTrack;
    private int mChannels = 2;
    private int mSampleRate = 48000;

    public AudioDevice() {}

    private int fetchChannelConfigFromCount(int channels) {
        if (channels == 1) return AudioFormat.CHANNEL_OUT_MONO;
        if (channels == 2) return AudioFormat.CHANNEL_OUT_STEREO;
        if (channels == 6) return AudioFormat.CHANNEL_OUT_5POINT1;
        if (channels == 8) return AudioFormat.CHANNEL_OUT_7POINT1_SURROUND;
        return AudioFormat.CHANNEL_OUT_STEREO;
    }

    public synchronized boolean init(int channels, int sampleRate, int bufferLength, int bufferCount) {
        close();
        mChannels = channels > 0 ? channels : 2;
        mSampleRate = sampleRate > 0 ? sampleRate : 48000;
        int channelConfig = fetchChannelConfigFromCount(mChannels);
        int bufferBytes = bufferLength * bufferCount * mChannels * 2;
        int minBuf = AudioTrack.getMinBufferSize(mSampleRate, channelConfig, AudioFormat.ENCODING_PCM_16BIT);
        if (bufferBytes < minBuf) {
            bufferBytes = minBuf;
        }
        try {
            mTrack = new AudioTrack(AudioManager.STREAM_MUSIC, mSampleRate, channelConfig,
                                    AudioFormat.ENCODING_PCM_16BIT, bufferBytes, AudioTrack.MODE_STREAM);
            if (mTrack.getState() == AudioTrack.STATE_INITIALIZED) {
                mTrack.play();
                return true;
            }
        } catch (Throwable t) {
            android.util.Log.e("FMODAudioDevice", "init failed: " + t);
        }
        return false;
    }

    public synchronized void start() {
        if (mTrack != null && mTrack.getState() == AudioTrack.STATE_INITIALIZED) {
            try {
                mTrack.play();
            } catch (Throwable ignored) {}
        }
    }

    public synchronized void stop() {
        close();
    }

    public synchronized void write(short[] buffer, int size) {
        if (mTrack != null && buffer != null && size > 0) {
            mTrack.write(buffer, 0, Math.min(size, buffer.length));
        } else if (buffer != null && size > 0) {
            long nanos = (long) (((double) (size / (mChannels > 0 ? mChannels : 2)) * 1e9) /
                                 (mSampleRate > 0 ? mSampleRate : 48000));
            try {
                Thread.sleep(nanos / 1000000, (int) (nanos % 1000000));
            } catch (InterruptedException ignored) {}
        }
    }

    public synchronized void close() {
        if (mTrack != null) {
            try {
                mTrack.stop();
                mTrack.release();
            } catch (Throwable ignored) {}
            mTrack = null;
        }
    }
}
