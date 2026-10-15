package android.net;

import android.os.Parcel;
import android.os.Parcelable;

public class NetworkInfo implements Parcelable {
    public enum State { CONNECTING, CONNECTED, SUSPENDED, DISCONNECTING, DISCONNECTED, UNKNOWN }
    public enum DetailedState { IDLE, SCANNING, CONNECTING, AUTHENTICATING, OBTAINING_IPADDR, CONNECTED, SUSPENDED, DISCONNECTING, DISCONNECTED, FAILED, BLOCKED, VERIFYING_POOR_LINK, CAPTIVE_PORTAL_CHECK }

    private final int mType;
    private final int mSubtype;
    private final String mTypeName;
    private final String mSubtypeName;
    private boolean mConnected;

    public NetworkInfo(int type, int subtype, String typeName, String subtypeName) {
        this(type, subtype, typeName, subtypeName, true);
    }

    NetworkInfo(int type, int subtype, String typeName, String subtypeName, boolean connected) {
        mType = type;
        mSubtype = subtype;
        mTypeName = typeName;
        mSubtypeName = subtypeName;
        mConnected = connected;
    }

    public int getType() { return mType; }
    public int getSubtype() { return mSubtype; }
    public String getTypeName() { return mTypeName; }
    public String getSubtypeName() { return mSubtypeName; }
    public boolean isConnected() { return mConnected; }
    public boolean isConnectedOrConnecting() { return mConnected; }
    public boolean isAvailable() { return mConnected; }
    public boolean isRoaming() { return false; }
    public boolean isFailover() { return false; }
    public String getReason() { return null; }
    public String getExtraInfo() { return null; }
    public State getState() { return mConnected ? State.CONNECTED : State.DISCONNECTED; }
    public DetailedState getDetailedState() {
        return mConnected ? DetailedState.CONNECTED : DetailedState.DISCONNECTED;
    }
    public int describeContents() { return 0; }
    public void writeToParcel(Parcel dest, int flags) {}

    @Override
    public String toString() {
        return "[type: " + mTypeName + "[" + mSubtypeName + "], state: " + getState() + "]";
    }
}
