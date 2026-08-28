package android.net;

import android.os.Parcel;
import android.os.Parcelable;

public final class NetworkCapabilities implements Parcelable {
    public static final int NET_CAPABILITY_INTERNET = 12;
    public static final int NET_CAPABILITY_NOT_RESTRICTED = 13;
    public static final int NET_CAPABILITY_VALIDATED = 16;
    public static final int TRANSPORT_CELLULAR = 0;
    public static final int TRANSPORT_WIFI = 1;
    public static final int TRANSPORT_ETHERNET = 3;

    public NetworkCapabilities() {}
    public boolean hasCapability(int capability) { return true; }
    public boolean hasTransport(int transportType) { return true; }
    public int describeContents() { return 0; }
    public void writeToParcel(Parcel dest, int flags) {}
}
