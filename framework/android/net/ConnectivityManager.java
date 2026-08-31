package android.net;

import android.os.Handler;
import java.util.ArrayList;

public class ConnectivityManager {
    public static final int TYPE_MOBILE = 0;
    public static final int TYPE_WIFI = 1;
    public static final int TYPE_ETHERNET = 9;

    public static class NetworkCallback {
        public void onAvailable(Network network) {}
        public void onLosing(Network network, int maxMsToLive) {}
        public void onLost(Network network) {}
        public void onUnavailable() {}
        public void onCapabilitiesChanged(Network network, NetworkCapabilities networkCapabilities) {}
        public void onLinkPropertiesChanged(Network network, LinkProperties linkProperties) {}
        public void onBlockedStatusChanged(Network network, boolean blocked) {}
    }

    private final ArrayList<NetworkCallback> mCallbacks = new ArrayList<NetworkCallback>();

    public ConnectivityManager() {}
    public NetworkInfo getActiveNetworkInfo() { return new NetworkInfo(TYPE_WIFI, 0, "WIFI", ""); }
    public NetworkInfo getNetworkInfo(int networkType) { return new NetworkInfo(networkType, 0, "WIFI", ""); }
    public Network getActiveNetwork() { return new Network(); }
    public NetworkCapabilities getNetworkCapabilities(Network network) { return new NetworkCapabilities(); }
    public LinkProperties getLinkProperties(Network network) { return new LinkProperties(); }
    public Network[] getAllNetworks() { return new Network[]{ new Network() }; }

    public void registerDefaultNetworkCallback(final NetworkCallback networkCallback) {
        registerDefaultNetworkCallback(networkCallback, null);
    }
    public void registerDefaultNetworkCallback(final NetworkCallback networkCallback, final Handler handler) {
        if (networkCallback == null) return;
        mCallbacks.add(networkCallback);
        Runnable r = new Runnable() {
            @Override
            public void run() {
                try {
                    Network net = getActiveNetwork();
                    networkCallback.onAvailable(net);
                    networkCallback.onCapabilitiesChanged(net, getNetworkCapabilities(net));
                    networkCallback.onLinkPropertiesChanged(net, getLinkProperties(net));
                } catch (Throwable ignored) {}
            }
        };
        if (handler != null) {
            handler.post(r);
        } else {
            android.os.Looper looper = android.os.Looper.getMainLooper();
            if (looper != null) {
                new Handler(looper).post(r);
            }
        }
    }
    public void registerNetworkCallback(NetworkRequest request, NetworkCallback networkCallback) {
        registerDefaultNetworkCallback(networkCallback, null);
    }
    public void registerNetworkCallback(NetworkRequest request, NetworkCallback networkCallback, Handler handler) {
        registerDefaultNetworkCallback(networkCallback, handler);
    }
    public void unregisterNetworkCallback(NetworkCallback networkCallback) {
        if (networkCallback != null) mCallbacks.remove(networkCallback);
    }
}
