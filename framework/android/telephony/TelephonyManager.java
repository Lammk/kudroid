package android.telephony;

public class TelephonyManager {
    public static final int SIM_STATE_UNKNOWN = 0;
    public static final int SIM_STATE_ABSENT = 1;
    public static final int SIM_STATE_PIN_REQUIRED = 2;
    public static final int SIM_STATE_PUK_REQUIRED = 3;
    public static final int SIM_STATE_NETWORK_LOCKED = 4;
    public static final int SIM_STATE_READY = 5;

    public static final int PHONE_TYPE_NONE = 0;
    public static final int PHONE_TYPE_GSM = 1;
    public static final int PHONE_TYPE_CDMA = 2;
    public static final int PHONE_TYPE_SIP = 3;

    public static final int NETWORK_TYPE_UNKNOWN = 0;
    public static final int NETWORK_TYPE_LTE = 13;
    public static final int NETWORK_TYPE_NR = 20;

    public TelephonyManager() {}
    public String getDeviceId() { return "000000000000000"; }
    public String getImei() { return "000000000000000"; }
    public String getSimOperator() { return "45204"; }
    public String getSimOperatorName() { return "Viettel"; }
    public String getSimCountryIso() { return "vn"; }
    public int getSimState() { return SIM_STATE_READY; }
    public int getPhoneType() { return PHONE_TYPE_GSM; }
    public static final int CALL_STATE_IDLE = 0;
    public static final int CALL_STATE_RINGING = 1;
    public static final int CALL_STATE_OFFHOOK = 2;

    public void listen(PhoneStateListener listener, int events) {}

    public int getDataState() { return 2; } // DATA_CONNECTED
}
