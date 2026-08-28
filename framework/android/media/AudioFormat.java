package android.media;

import android.os.Parcel;
import android.os.Parcelable;

public final class AudioFormat implements Parcelable {
    public static final int ENCODING_DEFAULT = 1;
    public static final int ENCODING_PCM_16BIT = 2;
    public static final int ENCODING_PCM_8BIT = 3;
    public static final int ENCODING_PCM_FLOAT = 4;
    public static final int ENCODING_AC3 = 5;
    public static final int ENCODING_E_AC3 = 6;
    public static final int ENCODING_DTS = 7;
    public static final int ENCODING_DTS_HD = 8;
    public static final int ENCODING_MP3 = 9;
    public static final int ENCODING_AAC_LC = 10;

    public static final int CHANNEL_OUT_FRONT_LEFT = 0x4;
    public static final int CHANNEL_OUT_FRONT_RIGHT = 0x8;
    public static final int CHANNEL_OUT_MONO = CHANNEL_OUT_FRONT_LEFT;
    public static final int CHANNEL_OUT_STEREO = (CHANNEL_OUT_FRONT_LEFT | CHANNEL_OUT_FRONT_RIGHT);
    public static final int CHANNEL_IN_MONO = 0x10;
    public static final int CHANNEL_IN_STEREO = 0xC;

    private int mEncoding = ENCODING_PCM_16BIT;
    private int mSampleRate = 44100;
    private int mChannelMask = CHANNEL_OUT_STEREO;

    public AudioFormat() {}
    public int getEncoding() { return mEncoding; }
    public int getSampleRate() { return mSampleRate; }
    public int getChannelMask() { return mChannelMask; }
    public int getChannelCount() { return (mChannelMask == CHANNEL_OUT_STEREO || mChannelMask == CHANNEL_IN_STEREO) ? 2 : 1; }

    public static class Builder {
        private final AudioFormat mFormat = new AudioFormat();
        public Builder() {}
        public Builder setEncoding(int encoding) { mFormat.mEncoding = encoding; return this; }
        public Builder setSampleRate(int sampleRate) { mFormat.mSampleRate = sampleRate; return this; }
        public Builder setChannelMask(int channelMask) { mFormat.mChannelMask = channelMask; return this; }
        public AudioFormat build() { return mFormat; }
    }

    public int describeContents() { return 0; }
    public void writeToParcel(Parcel dest, int flags) {}
}
