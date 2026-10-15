package android.net;

import android.os.Parcel;
import android.os.Parcelable;

public final class NetworkCapabilities implements Parcelable {
    public static final int NET_CAPABILITY_INTERNET = 12;
    public static final int NET_CAPABILITY_NOT_METERED = 11;
    public static final int NET_CAPABILITY_NOT_RESTRICTED = 13;
    public static final int NET_CAPABILITY_TRUSTED = 14;
    public static final int NET_CAPABILITY_NOT_VPN = 15;
    public static final int NET_CAPABILITY_VALIDATED = 16;
    public static final int NET_CAPABILITY_NOT_ROAMING = 18;
    public static final int NET_CAPABILITY_FOREGROUND = 19;
    public static final int NET_CAPABILITY_NOT_CONGESTED = 20;
    public static final int NET_CAPABILITY_NOT_SUSPENDED = 21;
    public static final int TRANSPORT_CELLULAR = 0;
    public static final int TRANSPORT_WIFI = 1;
    public static final int TRANSPORT_BLUETOOTH = 2;
    public static final int TRANSPORT_ETHERNET = 3;
    public static final int TRANSPORT_VPN = 4;

    private long mCapabilities;
    private long mTransports;

    public NetworkCapabilities() {}

    /** The capabilities of the host's default network. */
    static NetworkCapabilities forPath(int transportType, boolean metered) {
        final NetworkCapabilities nc = new NetworkCapabilities();
        nc.mTransports = 1L << transportType;
        for (int cap : new int[] { NET_CAPABILITY_INTERNET, NET_CAPABILITY_NOT_RESTRICTED,
                NET_CAPABILITY_TRUSTED, NET_CAPABILITY_NOT_VPN, NET_CAPABILITY_VALIDATED,
                NET_CAPABILITY_NOT_ROAMING, NET_CAPABILITY_FOREGROUND,
                NET_CAPABILITY_NOT_CONGESTED, NET_CAPABILITY_NOT_SUSPENDED }) {
            nc.mCapabilities |= 1L << cap;
        }
        if (!metered) nc.mCapabilities |= 1L << NET_CAPABILITY_NOT_METERED;
        return nc;
    }

    public boolean hasCapability(int capability) {
        return capability >= 0 && capability < 64 && (mCapabilities & (1L << capability)) != 0;
    }

    public boolean hasTransport(int transportType) {
        return transportType >= 0 && transportType < 64 && (mTransports & (1L << transportType)) != 0;
    }

    public int describeContents() { return 0; }
    public void writeToParcel(Parcel dest, int flags) {}
}
