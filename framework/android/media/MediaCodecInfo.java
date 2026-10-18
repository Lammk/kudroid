package android.media;

import android.util.Range;

/**
 * Codec capability description. KuDroid's decoders are placeholders (see MediaShim), so what
 * is reported is the set of formats they accept; the point of the class is that players can
 * read their constants and query it instead of failing on a missing class.
 */
public final class MediaCodecInfo {
    private final String mName;
    private final boolean mEncoder;
    private final String[] mTypes;

    MediaCodecInfo(String name, boolean encoder, String[] types) {
        mName = name;
        mEncoder = encoder;
        mTypes = types;
    }

    public String getName() { return mName; }
    public String getCanonicalName() { return mName; }
    public boolean isAlias() { return false; }
    public boolean isEncoder() { return mEncoder; }
    public boolean isVendor() { return false; }
    public boolean isSoftwareOnly() { return false; }
    public boolean isHardwareAccelerated() { return true; }
    public String[] getSupportedTypes() { return mTypes.clone(); }

    public CodecCapabilities getCapabilitiesForType(String type) {
        for (int i = 0; i < mTypes.length; i++) {
            if (mTypes[i].equalsIgnoreCase(type)) return new CodecCapabilities(type, mEncoder);
        }
        throw new IllegalArgumentException("codec does not support type");
    }

    public static final class CodecProfileLevel {
        public static final int AVCProfileBaseline = 0x01;
        public static final int AVCProfileMain = 0x02;
        public static final int AVCProfileExtended = 0x04;
        public static final int AVCProfileHigh = 0x08;
        public static final int AVCProfileHigh10 = 0x10;
        public static final int AVCProfileHigh422 = 0x20;
        public static final int AVCProfileHigh444 = 0x40;
        public static final int AVCLevel1 = 0x01;
        public static final int AVCLevel11 = 0x04;
        public static final int AVCLevel2 = 0x40;
        public static final int AVCLevel3 = 0x200;
        public static final int AVCLevel31 = 0x400;
        public static final int AVCLevel4 = 0x1000;
        public static final int AVCLevel41 = 0x2000;
        public static final int AVCLevel42 = 0x4000;
        public static final int AVCLevel5 = 0x8000;
        public static final int AVCLevel51 = 0x10000;
        public static final int AVCLevel52 = 0x20000;
        public static final int HEVCProfileMain = 0x01;
        public static final int HEVCProfileMain10 = 0x02;
        public static final int HEVCMainTierLevel4 = 0x100;
        public static final int HEVCMainTierLevel5 = 0x400;
        public static final int HEVCMainTierLevel51 = 0x1000;
        public static final int VP8ProfileMain = 0x01;
        public static final int VP8Level_Version0 = 0x01;
        public static final int VP9Profile0 = 0x01;
        public static final int VP9Profile2 = 0x04;
        public static final int VP9Level41 = 0x100;
        public static final int VP9Level5 = 0x200;
        public static final int AACObjectLC = 2;
        public static final int AACObjectHE = 5;

        public int profile;
        public int level;
    }

    public static final class CodecCapabilities {
        public static final int COLOR_Format12bitRGB444 = 3;
        public static final int COLOR_Format16bitARGB1555 = 5;
        public static final int COLOR_Format16bitARGB4444 = 4;
        public static final int COLOR_Format16bitBGR565 = 7;
        public static final int COLOR_Format16bitRGB565 = 6;
        public static final int COLOR_Format18BitBGR666 = 41;
        public static final int COLOR_Format18bitARGB1665 = 9;
        public static final int COLOR_Format18bitRGB666 = 8;
        public static final int COLOR_Format19bitARGB1666 = 10;
        public static final int COLOR_Format24BitABGR6666 = 43;
        public static final int COLOR_Format24BitARGB6666 = 42;
        public static final int COLOR_Format24bitARGB1887 = 13;
        public static final int COLOR_Format24bitBGR888 = 12;
        public static final int COLOR_Format24bitRGB888 = 11;
        public static final int COLOR_Format25bitARGB1888 = 14;
        public static final int COLOR_Format32bitABGR8888 = 0x7F00A000;
        public static final int COLOR_Format32bitARGB8888 = 16;
        public static final int COLOR_Format32bitBGRA8888 = 15;
        public static final int COLOR_Format8bitRGB332 = 2;
        public static final int COLOR_FormatCbYCrY = 27;
        public static final int COLOR_FormatCrYCbY = 28;
        public static final int COLOR_FormatL16 = 36;
        public static final int COLOR_FormatL2 = 33;
        public static final int COLOR_FormatL24 = 37;
        public static final int COLOR_FormatL32 = 38;
        public static final int COLOR_FormatL4 = 34;
        public static final int COLOR_FormatL8 = 35;
        public static final int COLOR_FormatMonochrome = 1;
        public static final int COLOR_FormatRGBAFlexible = 0x7F36A888;
        public static final int COLOR_FormatRGBFlexible = 0x7F36B888;
        public static final int COLOR_FormatRawBayer10bit = 31;
        public static final int COLOR_FormatRawBayer8bit = 30;
        public static final int COLOR_FormatRawBayer8bitcompressed = 32;
        public static final int COLOR_FormatSurface = 0x7F000789;
        public static final int COLOR_FormatYCbYCr = 25;
        public static final int COLOR_FormatYCrYCb = 26;
        public static final int COLOR_FormatYUV411PackedPlanar = 18;
        public static final int COLOR_FormatYUV411Planar = 17;
        public static final int COLOR_FormatYUV420Flexible = 0x7F420888;
        public static final int COLOR_FormatYUV420PackedPlanar = 20;
        public static final int COLOR_FormatYUV420PackedSemiPlanar = 39;
        public static final int COLOR_FormatYUV420Planar = 19;
        public static final int COLOR_FormatYUV420SemiPlanar = 21;
        public static final int COLOR_FormatYUV422Flexible = 0x7F422888;
        public static final int COLOR_FormatYUV422PackedPlanar = 23;
        public static final int COLOR_FormatYUV422PackedSemiPlanar = 40;
        public static final int COLOR_FormatYUV422Planar = 22;
        public static final int COLOR_FormatYUV422SemiPlanar = 24;
        public static final int COLOR_FormatYUV444Flexible = 0x7F444888;
        public static final int COLOR_FormatYUV444Interleaved = 29;
        public static final int COLOR_QCOM_FormatYUV420SemiPlanar = 0x7FA30C00;
        public static final int COLOR_TI_FormatYUV420PackedSemiPlanar = 0x7F000100;

        public static final String FEATURE_AdaptivePlayback = "adaptive-playback";
        public static final String FEATURE_DynamicTimestamp = "dynamic-timestamp";
        public static final String FEATURE_FrameParsing = "frame-parsing";
        public static final String FEATURE_IntraRefresh = "intra-refresh";
        public static final String FEATURE_LowLatency = "low-latency";
        public static final String FEATURE_MultipleFrames = "multiple-frames";
        public static final String FEATURE_PartialFrame = "partial-frame";
        public static final String FEATURE_QpBounds = "qp-bounds";
        public static final String FEATURE_SecurePlayback = "secure-playback";
        public static final String FEATURE_TunneledPlayback = "tunneled-playback";

        public int[] colorFormats;
        public CodecProfileLevel[] profileLevels;
        private final String mMime;
        private final boolean mEncoder;

        CodecCapabilities(String mime, boolean encoder) {
            mMime = mime;
            mEncoder = encoder;
            colorFormats = new int[] {COLOR_FormatSurface, COLOR_FormatYUV420Flexible,
                    COLOR_FormatYUV420Planar, COLOR_FormatYUV420SemiPlanar};
            profileLevels = new CodecProfileLevel[0];
        }

        public static CodecCapabilities createFromProfileLevel(String mime, int profile, int level) {
            CodecCapabilities caps = new CodecCapabilities(mime, false);
            CodecProfileLevel pl = new CodecProfileLevel();
            pl.profile = profile;
            pl.level = level;
            caps.profileLevels = new CodecProfileLevel[] {pl};
            return caps;
        }

        public String getMimeType() { return mMime; }
        public int getMaxSupportedInstances() { return 32; }
        public boolean isFeatureSupported(String name) { return false; }
        public boolean isFeatureRequired(String name) { return false; }
        public boolean isFormatSupported(MediaFormat format) { return true; }
        public MediaFormat getDefaultFormat() {
            MediaFormat f = new MediaFormat();
            f.setString(MediaFormat.KEY_MIME, mMime);
            return f;
        }
        public VideoCapabilities getVideoCapabilities() {
            return mMime.startsWith("video/") ? new VideoCapabilities() : null;
        }
        public AudioCapabilities getAudioCapabilities() {
            return mMime.startsWith("audio/") ? new AudioCapabilities() : null;
        }
        public EncoderCapabilities getEncoderCapabilities() {
            return mEncoder ? new EncoderCapabilities() : null;
        }
    }

    public static final class VideoCapabilities {
        VideoCapabilities() {}
        public Range<Integer> getBitrateRange() { return new Range<Integer>(Integer.valueOf(1), Integer.valueOf(100000000)); }
        public Range<Integer> getSupportedWidths() { return new Range<Integer>(Integer.valueOf(16), Integer.valueOf(8192)); }
        public Range<Integer> getSupportedHeights() { return new Range<Integer>(Integer.valueOf(16), Integer.valueOf(8192)); }
        public Range<Integer> getSupportedFrameRates() { return new Range<Integer>(Integer.valueOf(1), Integer.valueOf(240)); }
        public int getWidthAlignment() { return 2; }
        public int getHeightAlignment() { return 2; }
        public boolean isSizeSupported(int width, int height) { return width > 0 && height > 0 && width <= 8192 && height <= 8192; }
        public boolean areSizeAndRateSupported(int width, int height, double frameRate) { return isSizeSupported(width, height) && frameRate <= 240.0; }
    }

    public static final class AudioCapabilities {
        AudioCapabilities() {}
        public Range<Integer> getBitrateRange() { return new Range<Integer>(Integer.valueOf(1), Integer.valueOf(1000000)); }
        public int getMaxInputChannelCount() { return 8; }
        public int getMinInputChannelCount() { return 1; }
        public int[] getSupportedSampleRates() { return new int[] {8000, 16000, 22050, 32000, 44100, 48000}; }
        public boolean isSampleRateSupported(int sampleRate) { return sampleRate >= 8000 && sampleRate <= 96000; }
    }

    public static final class EncoderCapabilities {
        EncoderCapabilities() {}
    }
}
