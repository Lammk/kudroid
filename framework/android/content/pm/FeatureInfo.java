package android.content.pm;

import android.os.Parcel;
import android.os.Parcelable;

public class FeatureInfo implements Parcelable {
    public static final int FLAG_REQUIRED = 0x0001;
    public String name;
    public int version;
    public int reqGlEsVersion;
    public int flags;

    public FeatureInfo() {}
    public String getGlEsVersion() {
        int major = ((reqGlEsVersion & 0xffff0000) >> 16);
        int minor = reqGlEsVersion & 0x0000ffff;
        return String.valueOf(major) + "." + String.valueOf(minor);
    }
    public int describeContents() { return 0; }
    public void writeToParcel(Parcel dest, int flags) {}
}
