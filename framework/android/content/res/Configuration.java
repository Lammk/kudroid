package android.content.res;

/**
 * minimal android.content.res.configuration implementation.
 *
 * describes the device configuration (orientation, density, language). for
 * kudroid minimal framework, we provide reasonable defaults.
 */
public final class Configuration {
    /** screen orientation: undefined. */
    public static final int ORIENTATION_UNDEFINED = 0;
    /** screen orientation: portrait. */
    public static final int ORIENTATION_PORTRAIT = 1;
    /** screen orientation: landscape. */
    public static final int ORIENTATION_LANDSCAPE = 2;

    /** current screen orientation. */
    public int orientation = ORIENTATION_PORTRAIT;

    /** current screen density. */
    public int densityDpi = 420;

    /** current font scale. */
    public float fontScale = 1.0f;

    /** current language. */
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
