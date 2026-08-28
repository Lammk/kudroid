package android.content.res;

public class DisplayMetrics {
    public static final int DENSITY_LOW = 120;
    public static final int DENSITY_MEDIUM = 160;
    public static final int DENSITY_TV = 213;
    public static final int DENSITY_HIGH = 240;
    public static final int DENSITY_260 = 260;
    public static final int DENSITY_280 = 280;
    public static final int DENSITY_XHIGH = 320;
    public static final int DENSITY_340 = 340;
    public static final int DENSITY_360 = 360;
    public static final int DENSITY_400 = 400;
    public static final int DENSITY_420 = 420;
    public static final int DENSITY_440 = 440;
    public static final int DENSITY_XXHIGH = 480;
    public static final int DENSITY_560 = 560;
    public static final int DENSITY_XXXHIGH = 640;
    public static final int DENSITY_DEFAULT = DENSITY_MEDIUM;

    public int widthPixels = 1080;
    public int heightPixels = 1920;
    public float density = 2.0f;
    public int densityDpi = 320;
    public float scaledDensity = 2.0f;
    public float xdpi = 320.0f;
    public float ydpi = 320.0f;

    public DisplayMetrics() {}
    public void setTo(DisplayMetrics o) {
        widthPixels = o.widthPixels;
        heightPixels = o.heightPixels;
        density = o.density;
        densityDpi = o.densityDpi;
        scaledDensity = o.scaledDensity;
        xdpi = o.xdpi;
        ydpi = o.ydpi;
    }
    public void setToDefaults() {
        widthPixels = 1080;
        heightPixels = 1920;
        density = 2.0f;
        densityDpi = 320;
        scaledDensity = 2.0f;
        xdpi = 320.0f;
        ydpi = 320.0f;
    }
}
