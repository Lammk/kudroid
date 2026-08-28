package android.content.pm;

import android.os.Parcel;
import android.os.Parcelable;

public class ProviderInfo implements Parcelable {
    public String authority;
    public String readPermission;
    public String writePermission;
    public boolean grantUriPermissions;
    public String name;
    public String packageName;

    public ProviderInfo() {}
    public int describeContents() { return 0; }
    public void writeToParcel(Parcel dest, int flags) {}
}
