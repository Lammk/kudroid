package android.net.wifi;

/**
 * emulate android.net.wifi.wifimanager.
 *
 * is not important for application startup/rendering. Returns empty/default to the application
 * no problems when they query wifi status.
 */
public class WifiManager {
    /** wifi status: disabled. */
    public static final int WIFI_STATE_DISABLED = 1;
    /** wifi status: enabled. */
    public static final int WIFI_STATE_ENABLED = 3;

    public WifiManager() {
    }

    public boolean isWifiEnabled() {
        return false;
    }

    public int getWifiState() {
        return WIFI_STATE_DISABLED;
    }

    public WifiInfo getConnectionInfo() {
        return null;
    }

    public java.util.List<WifiConfiguration> getConfiguredNetworks() {
        return new java.util.ArrayList<WifiConfiguration>();
    }

    public boolean setWifiEnabled(boolean enabled) {
        return false;
    }

    public int addNetwork(WifiConfiguration config) {
        return -1;
    }

    public boolean removeNetwork(int netId) {
        return false;
    }

    public boolean enableNetwork(int netId, boolean disableOthers) {
        return false;
    }

    public boolean disconnect() {
        return false;
    }

    public boolean reconnect() {
        return false;
    }
}