package android.net;

import android.os.Parcel;
import android.os.Parcelable;

public class NetworkInfo implements Parcelable {
    public enum State { CONNECTING, CONNECTED, SUSPENDED, DISCONNECTING, DISCONNECTED, UNKNOWN }
    public enum DetailedState { IDLE, SCANNING, CONNECTING, AUTHENTICATING, OBTAINING_IPADDR, CONNECTED, SUSPENDED, DISCONNECTING, DISCONNECTED, FAILED, BLOCKED, VERIFYING_POOR_LINK, CAPTIVE_PORTAL_CHECK }

    private boolean mIsAvailable = true;
    private boolean mIsConnected = true;

    public NetworkInfo(int type, int subtype, String typeName, String subtypeName) {}
    public int getType() { return ConnectivityManager.TYPE_WIFI; }
    public String getTypeName() { return "WIFI"; }
    public boolean isConnected() { return mIsConnected; }
    public boolean isConnectedOrConnecting() { return mIsConnected; }
    public boolean isAvailable() { return mIsAvailable; }
    public State getState() { return State.CONNECTED; }
    public DetailedState getDetailedState() { return DetailedState.CONNECTED; }
    public int describeContents() { return 0; }
    public void writeToParcel(Parcel dest, int flags) {}
}
