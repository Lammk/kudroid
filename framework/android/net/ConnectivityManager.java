package android.net;

import android.os.Handler;
import android.os.Looper;
import java.util.ArrayList;

/**
 * Network state as the host reports it. The state is read on every call, so an app that
 * polls sees the device go offline, and registered callbacks are told on their Handler's
 * Looper when it changes.
 */
public class ConnectivityManager {
    public static final String CONNECTIVITY_ACTION = "android.net.conn.CONNECTIVITY_CHANGE";
    public static final String EXTRA_NO_CONNECTIVITY = "noConnectivity";
    public static final int TYPE_NONE = -1;
    public static final int TYPE_MOBILE = 0;
    public static final int TYPE_WIFI = 1;
    public static final int TYPE_BLUETOOTH = 7;
    public static final int TYPE_ETHERNET = 9;
    public static final int TYPE_VPN = 17;
    public static final int TYPE_DEFAULT = TYPE_MOBILE;
    public static final int RESTRICT_BACKGROUND_STATUS_DISABLED = 1;
    public static final int RESTRICT_BACKGROUND_STATUS_WHITELISTED = 2;
    public static final int RESTRICT_BACKGROUND_STATUS_ENABLED = 3;

    public static class NetworkCallback {
        public void onAvailable(Network network) {}
        public void onLosing(Network network, int maxMsToLive) {}
        public void onLost(Network network) {}
        public void onUnavailable() {}
        public void onCapabilitiesChanged(Network network, NetworkCapabilities networkCapabilities) {}
        public void onLinkPropertiesChanged(Network network, LinkProperties linkProperties) {}
        public void onBlockedStatusChanged(Network network, boolean blocked) {}
    }

    private static final class Registration {
        final NetworkCallback callback;
        final Handler handler;

        Registration(NetworkCallback callback, Handler handler) {
            this.callback = callback;
            this.handler = handler;
        }
    }

    private static native int nativeState();
    private static native int nativeTakeChange();

    private static final ArrayList<Registration> sRegistrations = new ArrayList<Registration>();
    private static int sLastState = -1;

    public ConnectivityManager() {}

    // State word: bit 0 connected, bits 1-8 transport (TYPE_*), bit 9 metered, bit 10 constrained.
    private static boolean connected(int state) { return (state & 1) != 0; }
    private static int type(int state) { return (state >> 1) & 0xFF; }
    private static boolean metered(int state) { return (state & (1 << 9)) != 0; }
    private static boolean constrained(int state) { return (state & (1 << 10)) != 0; }

    private static String typeName(int type) {
        switch (type) {
            case TYPE_MOBILE: return "MOBILE";
            case TYPE_ETHERNET: return "ETHERNET";
            default: return "WIFI";
        }
    }

    private static NetworkInfo info(int type, boolean connected) {
        return new NetworkInfo(type, 0, typeName(type), "", connected);
    }

    private static Network network(int state) {
        return connected(state) ? new Network(100 + type(state)) : null;
    }

    private static NetworkCapabilities capabilities(int state) {
        return NetworkCapabilities.forPath(type(state) == TYPE_MOBILE ? NetworkCapabilities.TRANSPORT_CELLULAR
                : type(state) == TYPE_ETHERNET ? NetworkCapabilities.TRANSPORT_ETHERNET
                : NetworkCapabilities.TRANSPORT_WIFI, metered(state));
    }

    /** The active network's info, or null when the device is offline. */
    public NetworkInfo getActiveNetworkInfo() {
        final int state = nativeState();
        return connected(state) ? info(type(state), true) : null;
    }

    public NetworkInfo getNetworkInfo(int networkType) {
        final int state = nativeState();
        if (networkType != TYPE_MOBILE && networkType != TYPE_WIFI && networkType != TYPE_ETHERNET) {
            return null;
        }
        return info(networkType, connected(state) && type(state) == networkType);
    }

    public NetworkInfo[] getAllNetworkInfo() {
        return new NetworkInfo[] { getNetworkInfo(TYPE_MOBILE), getNetworkInfo(TYPE_WIFI) };
    }

    public Network getActiveNetwork() { return network(nativeState()); }

    public Network[] getAllNetworks() {
        final Network network = network(nativeState());
        return network != null ? new Network[] { network } : new Network[0];
    }

    public NetworkCapabilities getNetworkCapabilities(Network network) {
        final int state = nativeState();
        return network != null && network.equals(network(state)) ? capabilities(state) : null;
    }

    public LinkProperties getLinkProperties(Network network) {
        final int state = nativeState();
        return network != null && network.equals(network(state)) ? new LinkProperties() : null;
    }

    public boolean isActiveNetworkMetered() {
        final int state = nativeState();
        return connected(state) && metered(state);
    }

    public boolean isDefaultNetworkActive() { return connected(nativeState()); }

    public int getRestrictBackgroundStatus() {
        return constrained(nativeState()) ? RESTRICT_BACKGROUND_STATUS_ENABLED
                : RESTRICT_BACKGROUND_STATUS_DISABLED;
    }

    public boolean bindProcessToNetwork(Network network) { return true; }
    public Network getBoundNetworkForProcess() { return getActiveNetwork(); }
    public void reportNetworkConnectivity(Network network, boolean hasConnectivity) {}

    public void registerDefaultNetworkCallback(NetworkCallback networkCallback) {
        registerDefaultNetworkCallback(networkCallback, null);
    }

    public void registerDefaultNetworkCallback(NetworkCallback networkCallback, Handler handler) {
        if (networkCallback == null) return;
        final Handler target = handler != null ? handler : new Handler(Looper.getMainLooper());
        final Registration registration = new Registration(networkCallback, target);
        synchronized (sRegistrations) {
            sRegistrations.add(registration);
            if (sLastState == -1) sLastState = nativeState();
        }
        // A callback registered while online is told about the current network.
        final int state = nativeState();
        if (connected(state)) announce(registration, state);
    }

    public void registerNetworkCallback(NetworkRequest request, NetworkCallback networkCallback) {
        registerDefaultNetworkCallback(networkCallback, null);
    }

    public void registerNetworkCallback(NetworkRequest request, NetworkCallback networkCallback,
                                        Handler handler) {
        registerDefaultNetworkCallback(networkCallback, handler);
    }

    public void requestNetwork(NetworkRequest request, NetworkCallback networkCallback) {
        registerDefaultNetworkCallback(networkCallback, null);
    }

    public void unregisterNetworkCallback(NetworkCallback networkCallback) {
        if (networkCallback == null) return;
        synchronized (sRegistrations) {
            for (int i = sRegistrations.size() - 1; i >= 0; i--) {
                if (sRegistrations.get(i).callback == networkCallback) sRegistrations.remove(i);
            }
        }
    }

    private static void announce(final Registration registration, final int state) {
        registration.handler.post(new Runnable() {
            @Override public void run() {
                try {
                    final Network network = network(state);
                    registration.callback.onAvailable(network);
                    registration.callback.onCapabilitiesChanged(network, capabilities(state));
                    registration.callback.onLinkPropertiesChanged(network, new LinkProperties());
                } catch (Throwable t) {
                    android.util.Log.e("ConnectivityManager", "network callback failed: " + t);
                }
            }
        });
    }

    private static void lost(final Registration registration, final int state) {
        registration.handler.post(new Runnable() {
            @Override public void run() {
                try {
                    registration.callback.onLost(new Network(100 + type(state)));
                } catch (Throwable t) {
                    android.util.Log.e("ConnectivityManager", "network callback failed: " + t);
                }
            }
        });
    }

    /** Tells registered callbacks what changed; runs on the main Looper after the host reported. */
    public static void dispatchNetworkChange() {
        if (nativeTakeChange() == 0) return;
        final int now = nativeState();
        final Registration[] registrations;
        final int before;
        synchronized (sRegistrations) {
            before = sLastState == -1 ? now : sLastState;
            sLastState = now;
            registrations = sRegistrations.toArray(new Registration[sRegistrations.size()]);
        }
        for (Registration registration : registrations) {
            if (connected(before) && (!connected(now) || type(before) != type(now))) {
                lost(registration, before);
            }
            if (connected(now) && (!connected(before) || type(before) != type(now))) {
                announce(registration, now);
            } else if (connected(now) && metered(before) != metered(now)) {
                final Registration r = registration;
                final int state = now;
                r.handler.post(new Runnable() {
                    @Override public void run() {
                        try {
                            r.callback.onCapabilitiesChanged(network(state), capabilities(state));
                        } catch (Throwable t) {
                            android.util.Log.e("ConnectivityManager", "network callback failed: " + t);
                        }
                    }
                });
            }
        }
    }
}
