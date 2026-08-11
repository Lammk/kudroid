package android.util;

/**
 * Minimal android.util.DisplayMetrics implementation.
 *
 * Describes the display's size and density. For KuDroid, the values are set
 * from the actual iOS screen resolution via the native bridge.
 */
public class DisplayMetrics {
    /** The absolute width of the display in pixels. */
    public int widthPixels;
    /** The absolute height of the display in pixels. */
    public int heightPixels;
    /** The logical density of the display. */
    public float density;
    /** The screen density expressed as dots-per-inch. */
    public int densityDpi;
    /** The exact physical pixels per inch of the screen in the X dimension. */
    public float xdpi;
    /** The exact physical pixels per inch of the screen in the Y dimension. */
    public float ydpi;
    /** The reported display density prior to any scaling. */
    public float scaledDensity;

    public DisplayMetrics() {
        // Defaults; the native bridge updates these from the real screen.
        widthPixels = 1080;
        heightPixels = 1920;
        density = 3.0f;
        densityDpi = 420;
        xdpi = 420.0f;
        ydpi = 420.0f;
        scaledDensity = 3.0f;
    }

    /**
     * Set the display metrics from the native bridge.
     */
    public void setTo(int width, int height, float densityValue) {
        widthPixels = width;
        heightPixels = height;
        density = densityValue;
        densityDpi = (int) (densityValue * 160.0f);
        xdpi = densityValue * 160.0f;
        ydpi = densityValue * 160.0f;
        scaledDensity = densityValue;
    }
}
