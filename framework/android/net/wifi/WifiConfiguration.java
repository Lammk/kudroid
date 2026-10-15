package android.net.wifi;

/**
 * emulate android.net.wifi.wificonfiguration.
 *
 * Describes the wifi network configuration. for kudroid minimal framework,
 *This is a simulation.
 */
public class WifiConfiguration {
    /** security: open. */
    public static final int SECURITY_OPEN = 0;
    /** security: wep. */
    public static final int SECURITY_WEP = 1;
    /** security: wpa. */
    public static final int SECURITY_WPA = 2;

    /** network id. */
    public int networkId = -1;
    /** ssid. */
    public String SSID = "";
    /** bssid. */
    public String BSSID = "";
    /** pre-shared key. */
    public String preSharedKey = "";
    /** password. */
    public String password = "";
    /** security type. */
    public int security = SECURITY_OPEN;

    public WifiConfiguration() {
    }
}