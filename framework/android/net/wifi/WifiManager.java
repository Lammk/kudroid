package android.net.wifi;

public class WifiManager {
    public static final int WIFI_STATE_DISABLING = 0;
    public static final int WIFI_STATE_DISABLED = 1;
    public static final int WIFI_STATE_ENABLING = 2;
    public static final int WIFI_STATE_ENABLED = 3;
    public static final int WIFI_STATE_UNKNOWN = 4;

    public WifiManager() {}
    public boolean isWifiEnabled() { return true; }
    public int getWifiState() { return WIFI_STATE_ENABLED; }
    public WifiInfo getConnectionInfo() { return new WifiInfo(); }
}
