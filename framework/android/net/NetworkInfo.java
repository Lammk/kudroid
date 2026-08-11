package android.net;

/**
 * Stub android.net.NetworkInfo.
 *
 * Describes the state of a network. For KuDroid's minimal framework, this is
 * a stub.
 */
public class NetworkInfo {
    /** State: connecting. */
    public static final int STATE_CONNECTING = 0;
    /** State: connected. */
    public static final int STATE_CONNECTED = 1;
    /** State: suspended. */
    public static final int STATE_SUSPENDED = 2;
    /** State: disconnecting. */
    public static final int STATE_DISCONNECTING = 3;
    /** State: disconnected. */
    public static final int STATE_DISCONNECTED = 4;

    private final int mType;
    private final int mSubtype;
    private final String mTypeName;
    private final String mSubtypeName;
    private int mState = STATE_DISCONNECTED;
    private boolean mAvailable = false;

    public NetworkInfo(int type, int subtype, String typeName, String subtypeName) {
        mType = type;
        mSubtype = subtype;
        mTypeName = typeName;
        mSubtypeName = subtypeName;
    }

    public int getType() {
        return mType;
    }

    public int getSubtype() {
        return mSubtype;
    }

    public String getTypeName() {
        return mTypeName;
    }

    public String getSubtypeName() {
        return mSubtypeName;
    }

    public int getState() {
        return mState;
    }

    public boolean isConnected() {
        return mState == STATE_CONNECTED;
    }

    public boolean isConnectedOrConnecting() {
        return mState == STATE_CONNECTED || mState == STATE_CONNECTING;
    }

    public boolean isAvailable() {
        return mAvailable;
    }

    public boolean isFailover() {
        return false;
    }

    public boolean isRoaming() {
        return false;
    }
}