package android.content.pm;

import android.os.Parcel;
import android.os.Parcelable;

public class ServiceInfo implements Parcelable {
    public String name;
    public String packageName;
    public String permission;
    public int flags;

    public ServiceInfo() {}
    public int describeContents() { return 0; }
    public void writeToParcel(Parcel dest, int flags) {}
}
