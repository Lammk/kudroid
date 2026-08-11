package android.net.wifi;

/**
 * mô phỏng android.net.wifi.wifimanager.
 *
 * không quan trọng đối với khởi động/kết xuất ứng dụng. trả về rỗng/mặc định để các ứng dụng
 * không gặp sự cố khi chúng truy vấn trạng thái wifi.
 */
public class WifiManager {
    /** trạng thái wifi: bị vô hiệu hóa. */
    public static final int WIFI_STATE_DISABLED = 1;
    /** trạng thái wifi: được kích hoạt. */
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