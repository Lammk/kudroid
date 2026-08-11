package android.net;

/**
 * mô phỏng android.net.connectivitymanager.
 *
 * không quan trọng đối với khởi động/kết xuất ứng dụng. trả về giá trị mặc định để các ứng dụng
 * không gặp sự cố khi chúng truy vấn trạng thái mạng.
 */
public class ConnectivityManager {
    /** loại mạng: không có. */
    public static final int TYPE_NONE = -1;
    /** loại mạng: di động. */
    public static final int TYPE_MOBILE = 0;
    /** loại mạng: wifi. */
    public static final int TYPE_WIFI = 1;

    public ConnectivityManager() {
    }

    public NetworkInfo getActiveNetworkInfo() {
        return null;
    }

    public NetworkInfo getNetworkInfo(int networkType) {
        return null;
    }

    public boolean isActiveNetworkMetered() {
        return false;
    }

    public boolean isDefaultNetworkActive() {
        return true;
    }

    public Network getActiveNetwork() {
        return null;
    }

    public void registerDefaultNetworkCallback(ConnectivityManager.NetworkCallback callback) {
    }

    public void unregisterNetworkCallback(ConnectivityManager.NetworkCallback callback) {
    }

    /**
     * mô phỏng networkcallback.
     */
    public static class NetworkCallback {
        public void onAvailable(Network network) {
        }

        public void onLost(Network network) {
        }

        public void onCapabilitiesChanged(Network network, android.net.NetworkCapabilities networkCapabilities) {
        }
    }
}