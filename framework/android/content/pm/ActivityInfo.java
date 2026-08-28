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
