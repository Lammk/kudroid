package android.net;

/**
 * emulate android.net.connectivitymanager.
 *
 * is not important for application startup/rendering. Returns default value to the application
 * do not crash when they query network status.
 */
public class ConnectivityManager {
    /** network type: none. */
    public static final int TYPE_NONE = -1;
    /** network type: mobile. */
    public static final int TYPE_MOBILE = 0;
    /** network type: wifi. */
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
     * simulate networkcallback.
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