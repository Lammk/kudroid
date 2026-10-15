package android.graphics;

/**
 * minimal android.graphics.pixelformat implementation.
 *
 * defines pixel format constants. for kudroid minimal framework, provided
 * opacity constants used by drawable.
 */
public class PixelFormat {
    /** opaque. */
    public static final int OPAQUE = -1;
    /** blur. */
    public static final int TRANSLUCENT = -3;
    /** transparent. */
    public static final int TRANSPARENT = -2;
    /** undefined. */
    public static final int UNKNOWN = 0;

    /** format rgba_8888. */
    public static final int RGBA_8888 = 1;
    /** format rgbx_8888. */
    public static final int RGBX_8888 = 2;
    /** format rgb_888. */
    public static final int RGB_888 = 3;
    /** format rgb_565. */
    public static final int RGB_565 = 4;

    private PixelFormat() {
    }
}
