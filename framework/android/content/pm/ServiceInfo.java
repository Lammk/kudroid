package android.content.pm;

import android.os.Parcel;
import android.os.Parcelable;

public class ServiceInfo implements Parcelable {
    public String name;
    public String packageName;
    public String permission;
    public int flags;
    public boolean enabled = true;
    public boolean exported = true;
    public String processName;
    public ApplicationInfo applicationInfo;
    /** Meta-data declared in the manifest; never null so callers can index it. */
    public android.os.Bundle metaData = new android.os.Bundle();

    public ServiceInfo() {}
    public int describeContents() { return 0; }
    public void writeToParcel(Parcel dest, int flags) {}
}
