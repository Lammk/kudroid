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
    public int uiMode = UI_MODE_TYPE_NORMAL | UI_MODE_NIGHT_NO;
    public int screenWidthDp = 360;
    public int screenHeightDp = 640;
    public int smallestScreenWidthDp = 360;
    public int densityDpi = 320;

    public Configuration() {}
    public Configuration(Configuration init) { setTo(init); }
    public void setTo(Configuration o) {
        fontScale = o.fontScale;
        locale = o.locale;
        orientation = o.orientation;
        screenLayout = o.screenLayout;
        uiMode = o.uiMode;
        screenWidthDp = o.screenWidthDp;
        screenHeightDp = o.screenHeightDp;
        smallestScreenWidthDp = o.smallestScreenWidthDp;
        densityDpi = o.densityDpi;
    }
    public int compareTo(Configuration that) { return 0; }
    public int describeContents() { return 0; }
    public void writeToParcel(Parcel dest, int flags) {}
}
