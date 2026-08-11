package android.net.wifi;

/**
 * Stub android.net.wifi.WifiManager.
 *
 * Non-critical for app startup/rendering. Returns null/defaults so apps don't
 * crash when they query WiFi state.
 */
public class WifiManager {
    /** WiFi state: disabled. */
    public static final int WIFI_STATE_DISABLED = 1;
    /** WiFi state: enabled. */
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