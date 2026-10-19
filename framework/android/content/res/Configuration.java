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

    public float fontScale;
    public Locale locale;
    public int mcc;
    public int mnc;
    public int touchscreen;
    public int keyboard;
    public int keyboardHidden;
    public int hardKeyboardHidden;
    public int navigation;
    public int navigationHidden;
    public int orientation;
    public int screenLayout;
    public int colorMode;
    public int uiMode;
    public int screenWidthDp;
    public int screenHeightDp;
    public int smallestScreenWidthDp;
    public int densityDpi;

    public int updateFrom(Configuration delta) {
        Configuration before = new Configuration(this);
        if (delta.fontScale > 0) fontScale = delta.fontScale;
        if (delta.mcc != 0) mcc = delta.mcc;
        if (delta.mnc != 0) mnc = delta.mnc;
        if (delta.locale != null) locale = delta.locale;
        if (delta.touchscreen != 0) touchscreen = delta.touchscreen;
        if (delta.keyboard != 0) keyboard = delta.keyboard;
        if (delta.keyboardHidden != 0) keyboardHidden = delta.keyboardHidden;
        if (delta.hardKeyboardHidden != 0) hardKeyboardHidden = delta.hardKeyboardHidden;
        if (delta.navigation != 0) navigation = delta.navigation;
        if (delta.navigationHidden != 0) navigationHidden = delta.navigationHidden;
        if (delta.orientation != 0) orientation = delta.orientation;
        for (int mask : new int[] { 0x0f, 0x30, 0xc0, 0x300 }) {
            if ((delta.screenLayout & mask) != 0) screenLayout = (screenLayout & ~mask) | (delta.screenLayout & mask);
        }
        for (int mask : new int[] { 0x0f, 0x30 }) {
            if ((delta.uiMode & mask) != 0) uiMode = (uiMode & ~mask) | (delta.uiMode & mask);
        }
        for (int mask : new int[] { 0x03, 0x0c }) {
            if ((delta.colorMode & mask) != 0) colorMode = (colorMode & ~mask) | (delta.colorMode & mask);
        }
        if (delta.screenWidthDp != 0) screenWidthDp = delta.screenWidthDp;
        if (delta.screenHeightDp != 0) screenHeightDp = delta.screenHeightDp;
        if (delta.smallestScreenWidthDp != 0) smallestScreenWidthDp = delta.smallestScreenWidthDp;
        if (delta.densityDpi != 0) densityDpi = delta.densityDpi;
        int changed = 0;
        if (before.fontScale != fontScale) changed |= 0x40000000;
        if (before.mcc != mcc) changed |= 1;
        if (before.mnc != mnc) changed |= 2;
        if (before.locale == null ? locale != null : !before.locale.equals(locale)) changed |= 4;
        if (before.touchscreen != touchscreen) changed |= 8;
        if (before.keyboard != keyboard) changed |= 16;
        if (before.keyboardHidden != keyboardHidden || before.hardKeyboardHidden != hardKeyboardHidden
                || before.navigationHidden != navigationHidden) changed |= 32;
        if (before.navigation != navigation) changed |= 64;
        if (before.orientation != orientation) changed |= 128;
        if (((before.screenLayout ^ screenLayout) & ~0xc0) != 0) changed |= 256;
        if (((before.screenLayout ^ screenLayout) & 0xc0) != 0) changed |= 8192;
        if (before.uiMode != uiMode) changed |= 512;
        if (before.screenWidthDp != screenWidthDp || before.screenHeightDp != screenHeightDp) changed |= 1024;
        if (before.smallestScreenWidthDp != smallestScreenWidthDp) changed |= 2048;
        if (before.densityDpi != densityDpi) changed |= 4096;
        if (before.colorMode != colorMode) changed |= 16384;
        return changed;
    }

    public Configuration() {}
    public void setToDefaults() {
        fontScale = 1;
        locale = null;
        mcc = 0;
        mnc = 0;
        touchscreen = 0;
        keyboard = 0;
        keyboardHidden = 0;
        hardKeyboardHidden = 0;
        navigation = 0;
        navigationHidden = 0;
        orientation = 0;
        screenLayout = 0;
        colorMode = 0;
        uiMode = 0;
        screenWidthDp = 0;
        screenHeightDp = 0;
        smallestScreenWidthDp = 0;
        densityDpi = 0;
    }
    public void unset() { setToDefaults(); fontScale = 0; }

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
    public android.os.LocaleList getLocales() { return locale == null ? new android.os.LocaleList() : new android.os.LocaleList(locale); }
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
    public int diff(Configuration delta) {
        Configuration copy = new Configuration(this);
        int changed = copy.updateFrom(delta);
        if ((changed & 4) != 0) changed |= 8192;
        return changed;
    }

    public boolean equals(Configuration other) { return other != null && compareTo(other) == 0; }

    @Override
    public boolean equals(Object other) { return other instanceof Configuration && equals((Configuration) other); }

    public int compareTo(Configuration that) {
        if (that == null) throw new NullPointerException();
        int comparison = Float.compare(fontScale, that.fontScale);
        if (comparison != 0) return comparison;
        comparison = Integer.compare(mcc, that.mcc);
        if (comparison != 0) return comparison;
        comparison = Integer.compare(mnc, that.mnc);
        if (comparison != 0) return comparison;
        if (locale == null && that.locale != null) return 1;
        if (locale != null && that.locale == null) return -1;
        if (locale != null) {
            comparison = locale.getLanguage().compareTo(that.locale.getLanguage());
            if (comparison != 0) return comparison;
            comparison = locale.getCountry().compareTo(that.locale.getCountry());
            if (comparison != 0) return comparison;
            comparison = locale.toLanguageTag().compareTo(that.locale.toLanguageTag());
            if (comparison != 0) return comparison;
        }
        comparison = Integer.compare(touchscreen, that.touchscreen);
        if (comparison != 0) return comparison;
        comparison = Integer.compare(keyboard, that.keyboard);
        if (comparison != 0) return comparison;
        comparison = Integer.compare(keyboardHidden, that.keyboardHidden);
        if (comparison != 0) return comparison;
        comparison = Integer.compare(hardKeyboardHidden, that.hardKeyboardHidden);
        if (comparison != 0) return comparison;
        comparison = Integer.compare(navigation, that.navigation);
        if (comparison != 0) return comparison;
        comparison = Integer.compare(navigationHidden, that.navigationHidden);
        if (comparison != 0) return comparison;
        comparison = Integer.compare(orientation, that.orientation);
        if (comparison != 0) return comparison;
        comparison = Integer.compare(colorMode, that.colorMode);
        if (comparison != 0) return comparison;
        comparison = Integer.compare(screenLayout, that.screenLayout);
        if (comparison != 0) return comparison;
        comparison = Integer.compare(uiMode, that.uiMode);
        if (comparison != 0) return comparison;
        comparison = Integer.compare(screenWidthDp, that.screenWidthDp);
        if (comparison != 0) return comparison;
        comparison = Integer.compare(screenHeightDp, that.screenHeightDp);
        if (comparison != 0) return comparison;
        comparison = Integer.compare(smallestScreenWidthDp, that.smallestScreenWidthDp);
        if (comparison != 0) return comparison;
        comparison = Integer.compare(densityDpi, that.densityDpi);
        if (comparison != 0) return comparison;
        return 0;
    }

    @Override
    public int hashCode() {
        int hash = Float.floatToIntBits(fontScale);
        hash = 31 * hash + mcc;
        hash = 31 * hash + mnc;
        hash = 31 * hash + (locale == null ? 0 : locale.toLanguageTag().hashCode());
        hash = 31 * hash + touchscreen;
        hash = 31 * hash + keyboard;
        hash = 31 * hash + keyboardHidden;
        hash = 31 * hash + hardKeyboardHidden;
        hash = 31 * hash + navigation;
        hash = 31 * hash + navigationHidden;
        hash = 31 * hash + orientation;
        hash = 31 * hash + colorMode;
        hash = 31 * hash + screenLayout;
        hash = 31 * hash + uiMode;
        hash = 31 * hash + screenWidthDp;
        hash = 31 * hash + screenHeightDp;
        hash = 31 * hash + smallestScreenWidthDp;
        hash = 31 * hash + densityDpi;
        return hash;
    }
    public int describeContents() { return 0; }
    public void writeToParcel(Parcel dest, int flags) {}
}
