package android.net.wifi;

/**
 * mô phỏng android.net.wifi.wificonfiguration.
 *
 * mô tả cấu hình mạng wifi. đối với khuôn khổ tối thiểu của kudroid,
 * đây là một mô phỏng.
 */
public class WifiConfiguration {
    /** bảo mật: mở. */
    public static final int SECURITY_OPEN = 0;
    /** bảo mật: wep. */
    public static final int SECURITY_WEP = 1;
    /** bảo mật: wpa. */
    public static final int SECURITY_WPA = 2;

    /** id mạng. */
    public int networkId = -1;
    /** ssid. */
    public String SSID = "";
    /** bssid. */
    public String BSSID = "";
    /** khóa chia sẻ trước (pre-shared key). */
    public String preSharedKey = "";
    /** mật khẩu. */
    public String password = "";
    /** loại bảo mật. */
    public int security = SECURITY_OPEN;

    public WifiConfiguration() {
    }
}