package android.util;

/**
 * minimal android.util.displaymetrics implementation.
 *
 * describes the size and density of the screen. for kudroid, the values ​​are set
 * from actual ios screen resolution via native bridge.
 */
public class DisplayMetrics {
    /**
     * Actual screen size of running iOS device — shot from Swift
     * (UIScreen.main.bounds) via C++ (kudroid_set_metal_layer →
     * kudroid_jni_update_display_metrics at JVM initialization) go here.
     */
    public static int sWidthPixels = 1080;
    public static int sHeightPixels = 1920;
    public static float sDensity = 3.0f;

    /** the absolute width of the screen in pixels. */
    public int widthPixels;
    /** absolute height of the screen in pixels. */
    public int heightPixels;
    /** logical density of the screen. */
    public float density;
    /** screen density expressed in dots per inch. */
    public int densityDpi;
    /** the exact number of physical pixels per inch of the screen in the x direction. */
    public float xdpi;
    /** the exact number of physical pixels per inch of the screen in the y direction. */
    public float ydpi;
    /** reported screen density before scaling. */
    public float scaledDensity;

    public DisplayMetrics() {
        // Read from statics (updated by native) instead of hardcode.
        setTo(sWidthPixels, sHeightPixels, sDensity);
    }

    /**
     * Called from native (kudroid_jni.cpp) with actual UIScreen metrics.
     */
    public static void updateFromNative(int width, int height, float densityValue) {
        sWidthPixels = width;
        sHeightPixels = height;
        sDensity = densityValue;
    }

    /**
     * set the metrics displayed from the root bridge.
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
