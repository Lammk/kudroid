package android.net;

/**
 * Stub android.net.ConnectivityManager.
 *
 * Non-critical for app startup/rendering. Returns defaults so apps don't
 * crash when they query network state.
 */
public class ConnectivityManager {
    /** Network type: none. */
    public static final int TYPE_NONE = -1;
    /** Network type: mobile. */
    public static final int TYPE_MOBILE = 0;
    /** Network type: wifi. */
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
     * Stub NetworkCallback.
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