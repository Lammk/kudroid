package android.graphics;

/**
 * triển khai android.graphics.pixelformat tối thiểu.
 *
 * định nghĩa các hằng số định dạng pixel. đối với khuôn khổ tối thiểu của kudroid, cung cấp
 * các hằng số độ mờ được sử dụng bởi drawable.
 */
public class PixelFormat {
    /** đục. */
    public static final int OPAQUE = -1;
    /** mờ. */
    public static final int TRANSLUCENT = -3;
    /** trong suốt. */
    public static final int TRANSPARENT = -2;
    /** không xác định. */
    public static final int UNKNOWN = 0;

    /** định dạng rgba_8888. */
    public static final int RGBA_8888 = 1;
    /** định dạng rgbx_8888. */
    public static final int RGBX_8888 = 2;
    /** định dạng rgb_888. */
    public static final int RGB_888 = 3;
    /** định dạng rgb_565. */
    public static final int RGB_565 = 4;

    private PixelFormat() {
    }
}
