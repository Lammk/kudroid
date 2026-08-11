package android.net.wifi;

/**
 * Stub android.net.wifi.WifiConfiguration.
 *
 * Describes a WiFi network configuration. For KuDroid's minimal framework,
 * this is a stub.
 */
public class WifiConfiguration {
    /** Security: open. */
    public static final int SECURITY_OPEN = 0;
    /** Security: WEP. */
    public static final int SECURITY_WEP = 1;
    /** Security: WPA. */
    public static final int SECURITY_WPA = 2;

    /** The network id. */
    public int networkId = -1;
    /** The SSID. */
    public String SSID = "";
    /** The BSSID. */
    public String BSSID = "";
    /** The pre-shared key. */
    public String preSharedKey = "";
    /** The password. */
    public String password = "";
    /** The security type. */
    public int security = SECURITY_OPEN;

    public WifiConfiguration() {
    }
}