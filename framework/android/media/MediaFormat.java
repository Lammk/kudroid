package android.media;

import java.nio.ByteBuffer;
import java.util.HashMap;
import java.util.Map;

public final class MediaFormat {
    public static final String KEY_MIME = "mime";
    public static final String KEY_WIDTH = "width";
    public static final String KEY_HEIGHT = "height";
    public static final String KEY_SAMPLE_RATE = "sample-rate";
    public static final String KEY_CHANNEL_COUNT = "channel-count";
    public static final String KEY_BIT_RATE = "bitrate";
    public static final String KEY_COLOR_FORMAT = "color-format";
    public static final String KEY_FRAME_RATE = "frame-rate";
    public static final String KEY_DURATION = "durationUs";
    public static final String KEY_MAX_INPUT_SIZE = "max-input-size";
    public static final String KEY_MAX_WIDTH = "max-width";
    public static final String KEY_MAX_HEIGHT = "max-height";
    public static final String KEY_ROTATION = "rotation-degrees";
    public static final String KEY_LANGUAGE = "language";
    public static final String KEY_I_FRAME_INTERVAL = "i-frame-interval";
    public static final String KEY_STRIDE = "stride";
    public static final String KEY_SLICE_HEIGHT = "slice-height";
    public static final String KEY_CHANNEL_MASK = "channel-mask";
    public static final String KEY_AAC_PROFILE = "aac-profile";
    public static final String KEY_IS_ADTS = "is-adts";
    public static final String KEY_IS_AUTOSELECT = "is-autoselect";
    public static final String KEY_IS_DEFAULT = "is-default";
    public static final String KEY_IS_FORCED_SUBTITLE = "is-forced-subtitle";
    public static final String KEY_FLAC_COMPRESSION_LEVEL = "flac-compression-level";
    public static final String KEY_PUSH_BLANK_BUFFERS_ON_STOP = "push-blank-buffers-on-stop";
    public static final String KEY_REPEAT_PREVIOUS_FRAME_AFTER = "repeat-previous-frame-after";
    public static final String KEY_COLOR_RANGE = "color-range";
    public static final String KEY_COLOR_STANDARD = "color-standard";
    public static final String KEY_COLOR_TRANSFER = "color-transfer";

    public static final int COLOR_RANGE_FULL = 1;
    public static final int COLOR_RANGE_LIMITED = 2;
    public static final int COLOR_STANDARD_BT709 = 1;
    public static final int COLOR_STANDARD_BT601_PAL = 2;
    public static final int COLOR_STANDARD_BT601_NTSC = 4;
    public static final int COLOR_STANDARD_BT2020 = 6;
    public static final int COLOR_TRANSFER_LINEAR = 1;
    public static final int COLOR_TRANSFER_SDR_VIDEO = 3;
    public static final int COLOR_TRANSFER_ST2084 = 6;
    public static final int COLOR_TRANSFER_HLG = 7;

    private final Map<String, Object> mMap = new HashMap<String, Object>();

    public MediaFormat() {}
    public static MediaFormat createAudioFormat(String mime, int sampleRate, int channelCount) {
        MediaFormat format = new MediaFormat();
        format.setString(KEY_MIME, mime);
        format.setInteger(KEY_SAMPLE_RATE, sampleRate);
        format.setInteger(KEY_CHANNEL_COUNT, channelCount);
        return format;
    }
    public static MediaFormat createVideoFormat(String mime, int width, int height) {
        MediaFormat format = new MediaFormat();
        format.setString(KEY_MIME, mime);
        format.setInteger(KEY_WIDTH, width);
        format.setInteger(KEY_HEIGHT, height);
        return format;
    }
    public void setInteger(String name, int value) { mMap.put(name, value); }
    public void setLong(String name, long value) { mMap.put(name, value); }
    public void setFloat(String name, float value) { mMap.put(name, value); }
    public void setString(String name, String value) { mMap.put(name, value); }
    public void setByteBuffer(String name, ByteBuffer bytes) { mMap.put(name, bytes); }
    public int getInteger(String name) { Object v = mMap.get(name); return v instanceof Number ? ((Number)v).intValue() : 0; }
    public int getInteger(String name, int defaultValue) { Object v = mMap.get(name); return v instanceof Number ? ((Number)v).intValue() : defaultValue; }
    public long getLong(String name) { Object v = mMap.get(name); return v instanceof Number ? ((Number)v).longValue() : 0L; }
    public float getFloat(String name) { Object v = mMap.get(name); return v instanceof Number ? ((Number)v).floatValue() : 0.0f; }
    public String getString(String name) { Object v = mMap.get(name); return v instanceof String ? (String)v : null; }
    public ByteBuffer getByteBuffer(String name) { Object v = mMap.get(name); return v instanceof ByteBuffer ? (ByteBuffer)v : null; }
    public boolean containsKey(String name) { return mMap.containsKey(name); }
}
