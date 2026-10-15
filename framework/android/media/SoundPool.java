package android.media;

import android.content.Context;

/**
 * SoundPool manages and plays audio resources following AOSP contract.
 */
public class SoundPool {

    public interface OnLoadCompleteListener {
        void onLoadComplete(SoundPool soundPool, int sampleId, int status);
    }

    private OnLoadCompleteListener mListener;
    private int mNextSoundId = 1;

    public SoundPool(int maxStreams, int streamType, int srcQuality) {}

    public void setOnLoadCompleteListener(OnLoadCompleteListener listener) {
        mListener = listener;
    }

    public int load(String path, int priority) {
        int id = mNextSoundId++;
        if (mListener != null) {
            mListener.onLoadComplete(this, id, 0);
        }
        return id;
    }

    public int load(Context context, int resId, int priority) {
        int id = mNextSoundId++;
        if (mListener != null) {
            mListener.onLoadComplete(this, id, 0);
        }
        return id;
    }

    public int play(int soundID, float leftVolume, float rightVolume, int priority, int loop, float rate) {
        return 1;
    }

    public void pause(int streamID) {}
    public void resume(int streamID) {}
    public void stop(int streamID) {}
    public void setVolume(int streamID, float leftVolume, float rightVolume) {}
    public void setRate(int streamID, float rate) {}
    public void setLoop(int streamID, int loop) {}
    public boolean unload(int soundID) { return true; }
    public void release() {}

    public static final class Builder {
        private int mMaxStreams = 1;
        private AudioAttributes mAudioAttributes;

        public Builder() {}

        public Builder setMaxStreams(int maxStreams) {
            mMaxStreams = maxStreams;
            return this;
        }

        public Builder setAudioAttributes(AudioAttributes attributes) {
            mAudioAttributes = attributes;
            return this;
        }

        public SoundPool build() {
            return new SoundPool(mMaxStreams, AudioManager.STREAM_MUSIC, 0);
        }
    }
}
