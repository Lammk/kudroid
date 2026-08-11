package android.telephony;

/**
 * mô phỏng android.telephony.telephonymanager.
 *
 * không quan trọng đối với khởi động/kết xuất ứng dụng. trả về mặc định để các ứng dụng không
 * gặp sự cố khi chúng truy vấn thông tin thiết bị/mạng.
 */
public class TelephonyManager {
    /** loại mạng: không xác định. */
    public static final int NETWORK_TYPE_UNKNOWN = 0;
    /** loại điện thoại: không có. */
    public static final int PHONE_TYPE_NONE = 0;
    /** trạng thái sim: không xác định. */
    public static final int SIM_STATE_UNKNOWN = 0;
    /** trạng thái sim: vắng mặt. */
    public static final int SIM_STATE_ABSENT = 1;
    /** trạng thái cuộc gọi: nhàn rỗi. */
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