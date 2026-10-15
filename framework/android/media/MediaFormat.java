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
