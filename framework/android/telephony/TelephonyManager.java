package android.telephony;

/**
 * Stub android.telephony.TelephonyManager.
 *
 * Non-critical for app startup/rendering. Returns defaults so apps don't
 * crash when they query device/network info.
 */
public class TelephonyManager {
    /** Network type: unknown. */
    public static final int NETWORK_TYPE_UNKNOWN = 0;
    /** Phone type: none. */
    public static final int PHONE_TYPE_NONE = 0;
    /** SIM state: unknown. */
    public static final int SIM_STATE_UNKNOWN = 0;
    /** SIM state: absent. */
    public static final int SIM_STATE_ABSENT = 1;
    /** Call state: idle. */
    public static final int CALL_STATE_IDLE = 0;

    public TelephonyManager() {
    }

    public String getDeviceId() {
        return null;
    }

    public String getImei() {
        return null;
    }

    public String getSubscriberId() {
        return null;
    }

    public String getLine1Number() {
        return null;
    }

    public String getNetworkOperator() {
        return "";
    }

    public String getNetworkOperatorName() {
        return "";
    }

    public String getNetworkCountryIso() {
        return "";
    }

    public int getNetworkType() {
        return NETWORK_TYPE_UNKNOWN;
    }

    public int getPhoneType() {
        return PHONE_TYPE_NONE;
    }

    public int getSimState() {
        return SIM_STATE_ABSENT;
    }

    public String getSimOperator() {
        return "";
    }

    public String getSimOperatorName() {
        return "";
    }

    public String getSimCountryIso() {
        return "";
    }

    public int getCallState() {
        return CALL_STATE_IDLE;
    }

    public int getDataState() {
        return 0;
    }

    public boolean isNetworkRoaming() {
        return false;
    }

    public String getDeviceSoftwareVersion() {
        return null;
    }

    public String getVoiceMailNumber() {
        return null;
    }
}