package android.media;

public final class AudioDeviceInfo {
    public static final int TYPE_UNKNOWN = 0;
    public static final int TYPE_BUILTIN_EARPIECE = 1;
    public static final int TYPE_BUILTIN_SPEAKER = 2;
    public static final int TYPE_WIRED_HEADSET = 3;
    public static final int TYPE_WIRED_HEADPHONES = 4;
    public static final int TYPE_LINE_ANALOG = 5;
    public static final int TYPE_LINE_DIGITAL = 6;
    public static final int TYPE_BLUETOOTH_SCO = 7;
    public static final int TYPE_BLUETOOTH_A2DP = 8;

    private final int mType;
    private final int mId;

    public AudioDeviceInfo() {
        this(TYPE_BUILTIN_SPEAKER, 1);
    }

    public AudioDeviceInfo(int type, int id) {
        this.mType = type;
        this.mId = id;
    }

    public int getId() { return mId; }
    public int getType() { return mType; }
    public boolean isSource() { return false; }
    public boolean isSink() { return true; }
    public CharSequence getProductName() { return "Built-in Speaker"; }
    public int[] getSampleRates() { return new int[]{44100, 48000}; }
    public int[] getChannelMasks() { return new int[]{1, 3}; }
    public int[] getChannelIndexMasks() { return new int[]{1, 3}; }
    public int[] getChannelCounts() { return new int[]{1, 2}; }
    public int[] getEncodings() { return new int[]{2}; }
}
