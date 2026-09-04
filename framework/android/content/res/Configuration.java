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

    // The `keyboard`, `touchscreen`, `navigation` and `*Hidden` fields below hold these
    // values. They are declared here rather than left out because an app READS the field
    // and compares it against the constant — an input layer deciding whether a hardware
    // keyboard is attached does `config.keyboard == Configuration.KEYBOARD_QWERTY`, and
    // without the constant the comparison cannot be compiled or resolved at all.
    public static final int KEYBOARD_UNDEFINED = 0;
    public static final int KEYBOARD_NOKEYS = 1;
    public static final int KEYBOARD_QWERTY = 2;
    public static final int KEYBOARD_12KEY = 3;

    public static final int KEYBOARDHIDDEN_NO = 1;
    public static final int KEYBOARDHIDDEN_YES = 2;

    public static final int HARDKEYBOARDHIDDEN_UNDEFINED = 0;
    public static final int HARDKEYBOARDHIDDEN_NO = 1;
    public static final int HARDKEYBOARDHIDDEN_YES = 2;

    public static final int NAVIGATION_NONAV = 1;
    public static final int NAVIGATION_DPAD = 2;
    public static final int NAVIGATION_TRACKBALL = 3;
    public static final int NAVIGATION_WHEEL = 4;

    public static final int NAVIGATIONHIDDEN_UNDEFINED = 0;
    public static final int NAVIGATIONHIDDEN_NO = 1;
    public static final int NAVIGATIONHIDDEN_YES = 2;

    public static final int TOUCHSCREEN_NOTOUCH = 1;
    public static final int TOUCHSCREEN_FINGER = 3;

    public static final int SCREENLAYOUT_LONG_UNDEFINED = 0x00;
    public static final int SCREENLAYOUT_LONG_MASK = 0x30;
    public static final int SCREENLAYOUT_LONG_NO = 0x10;
    public static final int SCREENLAYOUT_LONG_YES = 0x20;

    public static final int SCREENLAYOUT_ROUND_UNDEFINED = 0x00;
    public static final int SCREENLAYOUT_ROUND_MASK = 0x300;
    public static final int SCREENLAYOUT_ROUND_NO = 0x100;
    public static final int SCREENLAYOUT_ROUND_YES = 0x200;

    public static final int UI_MODE_TYPE_MASK = 0x0f;
    public static final int UI_MODE_TYPE_DESK = 0x02;
    public static final int UI_MODE_TYPE_CAR = 0x03;
    public static final int UI_MODE_TYPE_TELEVISION = 0x04;
    public static final int UI_MODE_TYPE_APPLIANCE = 0x05;
    public static final int UI_MODE_TYPE_WATCH = 0x06;
    public static final int UI_MODE_TYPE_VR_HEADSET = 0x07;
    public static final int UI_MODE_NIGHT_MASK = 0x30;
    public static final int UI_MODE_NIGHT_UNDEFINED = 0x00;

    public float fontScale = 1.0f;
    public int mcc;
    public int mnc;
    public Locale locale = Locale.getDefault();
    // Reported as the constants above, not as bare numbers: an app comparing the field
    // against Configuration.TOUCHSCREEN_FINGER must find them equal, and a magic number
    // here that drifts from the constant would silently make every such test false.
    public int touchscreen = TOUCHSCREEN_FINGER;
    // No hardware keyboard is attached to an iOS device by default, and saying otherwise
    // makes a game route input through a key handler that will never fire.
    public int keyboard = KEYBOARD_NOKEYS;
    public int keyboardHidden = KEYBOARDHIDDEN_YES;
    public int hardKeyboardHidden = HARDKEYBOARDHIDDEN_YES;
    public int navigation = NAVIGATION_NONAV;
    public int navigationHidden = NAVIGATIONHIDDEN_YES;
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
    /**
     * The locales, as {@link android.os.LocaleList}.
     *
     * AOSP changed the signature at API 24 from {@code Locale getLocale()} to
     * {@code LocaleList getLocales()}. This used to declare the new NAME with the
     * old TYPE, so any app built for API 24+ (Unity targets 30+) failed to resolve
     * {@code getLocales()Landroid/os/LocaleList;} through JNIBridge reflection —
     * a NoSuchMethodError out of the main Handler with no other trace.
     */
    public android.os.LocaleList getLocales() { return new android.os.LocaleList(locale); }
    /** Pre-24 spelling, kept for old callers. */
    public Locale getLocale() { return locale; }
    public void setLocale(Locale l) { if (l != null) locale = l; }
    public void setLocales(android.os.LocaleList list) {
        if (list != null && !list.isEmpty() && list.get(0) != null) locale = list.get(0);
    }
    public boolean isScreenRound() { return false; }
    public boolean isScreenHdr() { return (colorMode & COLOR_MODE_HDR_MASK) == COLOR_MODE_HDR_YES; }
    public boolean isScreenWideColorGamut() {
        return (colorMode & COLOR_MODE_WIDE_COLOR_GAMUT_MASK) == COLOR_MODE_WIDE_COLOR_GAMUT_YES;
    }
    public int compareTo(Configuration that) { return 0; }
    public int describeContents() { return 0; }
    public void writeToParcel(Parcel dest, int flags) {}
}
