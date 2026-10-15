package android.telephony;

public class PhoneStateListener {
    public static final int LISTEN_NONE = 0;
    public static final int LISTEN_SERVICE_STATE = 1;
    public static final int LISTEN_SIGNAL_STRENGTH = 2;
    public static final int LISTEN_MESSAGE_WAITING_INDICATOR = 4;
    public static final int LISTEN_CALL_FORWARDING_INDICATOR = 8;
    public static final int LISTEN_CELL_LOCATION = 16;
    public static final int LISTEN_CALL_STATE = 32;
    public static final int LISTEN_DATA_CONNECTION_STATE = 64;
    public static final int LISTEN_DATA_ACTIVITY = 128;
    public static final int LISTEN_SIGNAL_STRENGTHS = 256;

    public PhoneStateListener() {}
    public void onCallStateChanged(int state, String phoneNumber) {}
    public void onDataConnectionStateChanged(int state) {}
    public void onDataConnectionStateChanged(int state, int networkType) {}
}
