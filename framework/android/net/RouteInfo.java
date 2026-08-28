package android.net;

import android.os.Parcel;
import android.os.Parcelable;
import java.net.InetAddress;

public final class RouteInfo implements Parcelable {
    private final InetAddress mGateway;
    private final String mInterface;

    public RouteInfo(InetAddress gateway) {
        this.mGateway = gateway;
        this.mInterface = "wlan0";
    }
    public InetAddress getGateway() { return mGateway; }
    public String getInterface() { return mInterface; }
    public boolean isDefaultRoute() { return true; }
    public int describeContents() { return 0; }
    public void writeToParcel(Parcel dest, int flags) {}
}
