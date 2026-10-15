package android.media;

import android.os.Handler;

/**
 * A class to encapsulate information about an audio focus request following AOSP specification.
 */
public final class AudioFocusRequest {
    private final AudioAttributes mAttr;
    private final int mFocusGain;
    private final AudioManager.OnAudioFocusChangeListener mListener;
    private final Handler mHandler;
    private final boolean mPausesOnDuck;
    private final boolean mDelayedFocus;

    private AudioFocusRequest(AudioAttributes attr, int focusGain,
                               AudioManager.OnAudioFocusChangeListener listener,
                               Handler handler, boolean pausesOnDuck, boolean delayedFocus) {
        mAttr = attr;
        mFocusGain = focusGain;
        mListener = listener;
        mHandler = handler;
        mPausesOnDuck = pausesOnDuck;
        mDelayedFocus = delayedFocus;
    }

    public AudioAttributes getAudioAttributes() {
        return mAttr;
    }

    public int getFocusGain() {
        return mFocusGain;
    }

    public boolean willPauseWhenDucked() {
        return mPausesOnDuck;
    }

    public boolean acceptsDelayedFocusGain() {
        return mDelayedFocus;
    }

    public AudioManager.OnAudioFocusChangeListener getOnAudioFocusChangeListener() {
        return mListener;
    }

    public static final class Builder {
        private AudioAttributes mAttr;
        private int mFocusGain;
        private AudioManager.OnAudioFocusChangeListener mListener;
        private Handler mHandler;
        private boolean mPausesOnDuck = false;
        private boolean mDelayedFocus = false;

        public Builder(int focusGain) {
            setFocusGain(focusGain);
        }

        public Builder(AudioFocusRequest requestToCopy) {
            if (requestToCopy != null) {
                mAttr = requestToCopy.mAttr;
                mFocusGain = requestToCopy.mFocusGain;
                mListener = requestToCopy.mListener;
                mHandler = requestToCopy.mHandler;
                mPausesOnDuck = requestToCopy.mPausesOnDuck;
                mDelayedFocus = requestToCopy.mDelayedFocus;
            }
        }

        public Builder setFocusGain(int focusGain) {
            mFocusGain = focusGain;
            return this;
        }

        public Builder setOnAudioFocusChangeListener(AudioManager.OnAudioFocusChangeListener listener) {
            return setOnAudioFocusChangeListener(listener, null);
        }

        public Builder setOnAudioFocusChangeListener(AudioManager.OnAudioFocusChangeListener listener,
                                                     Handler handler) {
            mListener = listener;
            mHandler = handler;
            return this;
        }

        public Builder setAudioAttributes(AudioAttributes attributes) {
            mAttr = attributes;
            return this;
        }

        public Builder setWillPauseWhenDucked(boolean pauseOnDuck) {
            mPausesOnDuck = pauseOnDuck;
            return this;
        }

        public Builder setAcceptsDelayedFocusGain(boolean acceptsDelayedFocusGain) {
            mDelayedFocus = acceptsDelayedFocusGain;
            return this;
        }

        public AudioFocusRequest build() {
            return new AudioFocusRequest(mAttr, mFocusGain, mListener, mHandler,
                                         mPausesOnDuck, mDelayedFocus);
        }
    }
}
