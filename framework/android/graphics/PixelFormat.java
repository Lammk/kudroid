package android.graphics;

/**
 * Minimal android.graphics.PixelFormat implementation.
 *
 * Defines pixel format constants. For KuDroid's minimal framework, provides
 * the opacity constants used by Drawable.
 */
public class PixelFormat {
    /** Opaque. */
    public static final int OPAQUE = -1;
    /** Translucent. */
    public static final int TRANSLUCENT = -3;
    /** Transparent. */
    public static final int TRANSPARENT = -2;
    /** Unknown. */
    public static final int UNKNOWN = 0;

    /** RGBA_8888 format. */
    public static final int RGBA_8888 = 1;
    /** RGBX_8888 format. */
    public static final int RGBX_8888 = 2;
    /** RGB_888 format. */
    public static final int RGB_888 = 3;
    /** RGB_565 format. */
    public static final int RGB_565 = 4;

    private PixelFormat() {
    }
}
