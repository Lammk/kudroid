package android.content.res;

/**
 * triển khai android.content.res.configuration tối thiểu.
 *
 * mô tả cấu hình thiết bị (hướng, mật độ, ngôn ngữ). đối với
 * khuôn khổ tối thiểu của kudroid, chúng tôi cung cấp các giá trị mặc định hợp lý.
 */
public final class Configuration {
    /** hướng màn hình: không xác định. */
    public static final int ORIENTATION_UNDEFINED = 0;
    /** hướng màn hình: dọc. */
    public static final int ORIENTATION_PORTRAIT = 1;
    /** hướng màn hình: ngang. */
    public static final int ORIENTATION_LANDSCAPE = 2;

    /** hướng màn hình hiện tại. */
    public int orientation = ORIENTATION_PORTRAIT;

    /** mật độ màn hình hiện tại. */
    public int densityDpi = 420;

    /** tỷ lệ phông chữ hiện tại. */
    public float fontScale = 1.0f;

    /** ngôn ngữ hiện tại. */
    public java.util.Locale locale = java.util.Locale.getDefault();

    public Configuration() {
    }

    public Configuration(Configuration o) {
        orientation = o.orientation;
        densityDpi = o.densityDpi;
        fontScale = o.fontScale;
        locale = o.locale;
    }
}
