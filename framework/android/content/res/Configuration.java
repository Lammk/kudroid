package android.content.res;

import android.os.Parcel;
import android.os.Parcelable;
import java.util.Locale;

public final class Configuration implements Parcelable, Comparable<Configuration> {
    public static final int ORIENTATION_UNDEFINED = 0;
    public static final int ORIENTATION_PORTRAIT = 1;
    public static final int ORIENTATION_LANDSCAPE = 2;

    public static final int SCREENLAYOUT_SIZE_UNDEFINED = 0;
    public static final int SCREENLAYOUT_SIZE_SMALL = 1;
    public static final int SCREENLAYOUT_SIZE_NORMAL = 2;
    public static final int SCREENLAYOUT_SIZE_LARGE = 3;
    public static final int SCREENLAYOUT_SIZE_XLARGE = 4;
    public static final int SCREENLAYOUT_SIZE_MASK = 15;

    public static final int UI_MODE_TYPE_NORMAL = 1;
    public static final int UI_MODE_NIGHT_NO = 16;
    public static final int UI_MODE_NIGHT_YES = 32;

    // Wide-gamut and HDR bits, read through colorMode below.
    public static final int COLOR_MODE_WIDE_COLOR_GAMUT_MASK = 3;
    public static final int COLOR_MODE_WIDE_COLOR_GAMUT_UNDEFINED = 0;
    public static final int COLOR_MODE_WIDE_COLOR_GAMUT_NO = 1;
    public static final int COLOR_MODE_WIDE_COLOR_GAMUT_YES = 2;
    public static final int COLOR_MODE_HDR_MASK = 12;
    public static final int COLOR_MODE_HDR_UNDEFINED = 0;
    public static final int COLOR_MODE_HDR_NO = 4;
    public static final int COLOR_MODE_HDR_YES = 8;
    public static final int COLOR_MODE_UNDEFINED = 0;

    public static final int DENSITY_DPI_UNDEFINED = 0;
    public static final int KEYBOARDHIDDEN_UNDEFINED = 0;
    public static final int NAVIGATION_UNDEFINED = 0;
    public static final int SCREENLAYOUT_UNDEFINED = 0;
    public static final int TOUCHSCREEN_UNDEFINED = 0;
    public static final int UI_MODE_TYPE_UNDEFINED = 0;

    public float fontScale = 1.0f;
    public int mcc;
    public int mnc;
    public Locale locale = Locale.getDefault();
    public int touchscreen = 3;
    public int keyboard = 1;
    public int keyboardHidden = 1;
    public int hardKeyboardHidden = 2;
    public int navigation = 1;
    public int navigationHidden = 2;
    public int orientation = ORIENTATION_PORTRAIT;
    public int screenLayout = SCREENLAYOUT_SIZE_NORMAL;

    /**
     * Wide-gamut and HDR capability bits.
     *
     * Read directly as a field, not through a getter, so it has to exist here: field
     * resolution walks the class and a missing one is a NoSuchFieldError. AGDK's
     * GameActivity.onCreate reads it while building the ActivityInfo it passes to native
     * code, which threw and aborted Activity creation for Minecraft.
     *
     * Reported as "neither" rather than undefined. A guest that asks is deciding whether
     * to allocate a wide-gamut or HDR surface, and KuDroid presents an sRGB SDR one; an
     * UNDEFINED answer invites it to probe further or guess, while NO is true and ends
     * the question.
     */
    public int colorMode = COLOR_MODE_WIDE_COLOR_GAMUT_NO | COLOR_MODE_HDR_NO;

    public int uiMode = UI_MODE_TYPE_NORMAL | UI_MODE_NIGHT_NO;
    public int screenWidthDp = 360;
    public int screenHeightDp = 640;
    public int smallestScreenWidthDp = 360;
    public int densityDpi = 320;

    public Configuration() {}
    public Configuration(Configuration init) { setTo(init); }
    public void setTo(Configuration o) {
        fontScale = o.fontScale;
        mcc = o.mcc;
        mnc = o.mnc;
        locale = o.locale;
        touchscreen = o.touchscreen;
        keyboard = o.keyboard;
        keyboardHidden = o.keyboardHidden;
        hardKeyboardHidden = o.hardKeyboardHidden;
        navigation = o.navigation;
        navigationHidden = o.navigationHidden;
        orientation = o.orientation;
        screenLayout = o.screenLayout;
        colorMode = o.colorMode;
        uiMode = o.uiMode;
        screenWidthDp = o.screenWidthDp;
        screenHeightDp = o.screenHeightDp;
        smallestScreenWidthDp = o.smallestScreenWidthDp;
        densityDpi = o.densityDpi;
    }
    public Locale getLocales() { return locale; }
    public boolean isScreenRound() { return false; }
    public boolean isScreenHdr() { return (colorMode & COLOR_MODE_HDR_MASK) == COLOR_MODE_HDR_YES; }
    public boolean isScreenWideColorGamut() {
        return (colorMode & COLOR_MODE_WIDE_COLOR_GAMUT_MASK) == COLOR_MODE_WIDE_COLOR_GAMUT_YES;
    }
    public int compareTo(Configuration that) { return 0; }
    public int describeContents() { return 0; }
    public void writeToParcel(Parcel dest, int flags) {}
}
