package android.content.res;

/**
 * Minimal android.content.res.Configuration implementation.
 *
 * Describes device configuration (orientation, density, locale). For
 * KuDroid's minimal framework, we provide sensible defaults.
 */
public final class Configuration {
    /** Screen orientation: undefined. */
    public static final int ORIENTATION_UNDEFINED = 0;
    /** Screen orientation: portrait. */
    public static final int ORIENTATION_PORTRAIT = 1;
    /** Screen orientation: landscape. */
    public static final int ORIENTATION_LANDSCAPE = 2;

    /** The current screen orientation. */
    public int orientation = ORIENTATION_PORTRAIT;

    /** The current screen density. */
    public int densityDpi = 420;

    /** The current font scale. */
    public float fontScale = 1.0f;

    /** The current locale. */
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
