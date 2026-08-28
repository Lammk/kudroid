package android.content.pm;

import android.os.Parcel;
import android.os.Parcelable;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;

public class ActivityInfo implements Parcelable {
    @Retention(RetentionPolicy.SOURCE)
    public @interface Config {}

    public String name;
    public String packageName;
    public ApplicationInfo applicationInfo;
    public int screenOrientation = 0;
    public int configChanges = 0;
    public int flags = 0;

    // Fields apps read off a resolved component. They were absent, so reading any of
    // them threw NoSuchFieldError from inside PackageManager result handling.
    /** Whether the component is enabled; a disabled component is skipped. */
    public boolean enabled = true;
    /** Whether other apps may start it. */
    public boolean exported = true;
    /** Permission required to start it, or null when none is. */
    public String permission;
    /** Activity to navigate up to, or null when there is none. */
    public String parentActivityName;
    /** android:process, or null for the default process. */
    public String processName;
    /** Task affinity, or null for the package default. */
    public String taskAffinity;
    /** Meta-data declared in the manifest; never null so callers can index it. */
    public android.os.Bundle metaData = new android.os.Bundle();
    /** Theme resource, 0 when unset. */
    public int theme;
    public int labelRes;
    public int icon;
    public int logo;
    public int launchMode;
    public int softInputMode;
    public int uiOptions;
    public CharSequence nonLocalizedLabel;

    public static final int SCREEN_ORIENTATION_UNSPECIFIED = -1;
    public static final int SCREEN_ORIENTATION_LANDSCAPE = 0;
    public static final int SCREEN_ORIENTATION_PORTRAIT = 1;
    public static final int SCREEN_ORIENTATION_USER = 2;
    public static final int SCREEN_ORIENTATION_BEHIND = 3;
    public static final int SCREEN_ORIENTATION_SENSOR = 4;
    public static final int SCREEN_ORIENTATION_NOSENSOR = 5;

    public ActivityInfo() {}
    public int describeContents() { return 0; }
    public void writeToParcel(Parcel dest, int flags) {}
}
