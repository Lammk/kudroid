package android.content.pm;

import android.os.Parcel;
import android.os.Parcelable;

public class ResolveInfo implements Parcelable {
    public ActivityInfo activityInfo;
    public ResolveInfo() {}
    public int describeContents() { return 0; }
    public void writeToParcel(Parcel dest, int flags) {}
}
