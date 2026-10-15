package android.content.pm;

import android.os.Parcel;
import android.os.Parcelable;

public class ConfigurationInfo implements Parcelable {
    public int reqTouchScreen;
    public int reqKeyboardType;
    public int reqNavigation;
    public int reqInputFeatures = 0;
    public int reqGlEsVersion = 0x00030000;

    public static final int GL_ES_VERSION_UNDEFINED = 0;
    public ConfigurationInfo() {}
    public String getGlEsVersion() { return "3.0"; }
    public int describeContents() { return 0; }
    public void writeToParcel(Parcel dest, int flags) {}
}
