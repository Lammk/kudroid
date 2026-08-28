package android.content.pm;

import android.os.Parcel;
import android.os.Parcelable;

public class PackageInfo implements Parcelable {
    public String packageName;
    public int versionCode;
    public long versionCodeMajor;
    public String versionName;
    public ApplicationInfo applicationInfo;
    public long firstInstallTime;
    public long lastUpdateTime;
    public int[] gids;
    public ActivityInfo[] activities;
    public String[] requestedPermissions;
    public int[] requestedPermissionsFlags;
    public Signature[] signatures;

    public PackageInfo() {}
    public long getLongVersionCode() { return ((long) versionCodeMajor << 32) | (versionCode & 0xFFFFFFFFL); }
    public int describeContents() { return 0; }
    public void writeToParcel(Parcel dest, int flags) {}
}
