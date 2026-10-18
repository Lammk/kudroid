package android.net;

import android.os.Handler;
import android.os.Looper;
import android.os.HandlerThread;
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
        final NetworkRequest request;
        volatile boolean active = true;
        int announcedState;
        boolean everAvailable;

        Registration(NetworkCallback callback, Handler handler, NetworkRequest request) {
            this.callback = callback;
            this.handler = handler;
            this.request = request;
        }
        boolean matches(int state) {
            return connected(state) && (request == null || request.canBeSatisfiedBy(capabilities(state)));
        }
    }

    private static native int nativeState();
    private static native int nativeTakeChange();

    private static final ArrayList<Registration> sRegistrations = new ArrayList<Registration>();
    private static Handler sCallbackHandler;
    private static Network sBoundNetwork;

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
        return new NetworkInfo[] { getNetworkInfo(TYPE_MOBILE), getNetworkInfo(TYPE_WIFI), getNetworkInfo(TYPE_ETHERNET) };
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

    public boolean bindProcessToNetwork(Network network) {
        if (network != null && !network.equals(getActiveNetwork())) return false;
        sBoundNetwork = network;
        return true;
    }
    public Network getBoundNetworkForProcess() { return sBoundNetwork; }
    public void reportNetworkConnectivity(Network network, boolean hasConnectivity) {}

    private static Handler callbackHandler() {
        synchronized (sRegistrations) {
            if (sCallbackHandler == null) {
                HandlerThread thread = new HandlerThread("ConnectivityCallbacks");
                thread.start();
                sCallbackHandler = new Handler(thread.getLooper());
            }
            return sCallbackHandler;
        }
    }

    private static Registration register(NetworkRequest request, NetworkCallback callback, Handler handler) {
        if (callback == null) throw new NullPointerException("network callback");
        final Registration registration = new Registration(callback, handler == null ? callbackHandler() : handler, request);
        synchronized (sRegistrations) {
            for (Registration existing : sRegistrations) {
                if (existing.callback == callback) throw new IllegalArgumentException("Callback is already registered");
            }
            sRegistrations.add(registration);
            update(registration, nativeState());
        }
        return registration;
    }
    public void registerDefaultNetworkCallback(NetworkCallback callback) { register(null, callback, null); }
    public void registerDefaultNetworkCallback(NetworkCallback callback, Handler handler) { register(null, callback, handler); }
    public void registerNetworkCallback(NetworkRequest request, NetworkCallback callback) { registerNetworkCallback(request, callback, null); }
    public void registerNetworkCallback(NetworkRequest request, NetworkCallback callback, Handler handler) {
        if (request == null) throw new NullPointerException("network request");
        register(request, callback, handler);
    }
    public void requestNetwork(NetworkRequest request, NetworkCallback callback) { registerNetworkCallback(request, callback); }
    public void requestNetwork(NetworkRequest request, NetworkCallback callback, Handler handler) { registerNetworkCallback(request, callback, handler); }
    public void requestNetwork(NetworkRequest request, NetworkCallback callback, int timeoutMs) { requestNetwork(request, callback, null, timeoutMs); }
    public void requestNetwork(NetworkRequest request, NetworkCallback callback, Handler handler, int timeoutMs) {
        if (request == null) throw new NullPointerException("network request");
        if (timeoutMs <= 0) throw new IllegalArgumentException("Timeout must be positive");
        final Registration registration = register(request, callback, handler);
        registration.handler.postDelayed(new Runnable() {
            public void run() {
                synchronized (sRegistrations) {
                    if (!registration.active || registration.everAvailable) return;
                    registration.active = false;
                    sRegistrations.remove(registration);
                }
                registration.callback.onUnavailable();
            }
        }, timeoutMs);
    }
    public void unregisterNetworkCallback(NetworkCallback callback) {
        if (callback == null) throw new NullPointerException("network callback");
        synchronized (sRegistrations) {
            for (int i = sRegistrations.size() - 1; i >= 0; --i) {
                Registration registration = sRegistrations.get(i);
                if (registration.callback == callback) {
                    registration.active = false;
                    sRegistrations.remove(i);
                }
            }
        }
    }

    // Capture each transition before another path update arrives.
    private static void update(final Registration registration, final int now) {
        final int before = registration.announcedState;
        final boolean matches = registration.matches(now);
        if (connected(before) && (!matches || type(before) != type(now))) {
            registration.handler.post(new Runnable() {
                public void run() {
                    if (registration.active) registration.callback.onLost(network(before));
                }
            });
        }
        if (matches) {
            final boolean added = !connected(before) || type(before) != type(now);
            registration.everAvailable = true;
            if (added || before != now) {
                registration.handler.post(new Runnable() {
                    public void run() {
                        if (!registration.active) return;
                        Network current = network(now);
                        if (added) registration.callback.onAvailable(current);
                        if (!registration.active) return;
                        registration.callback.onCapabilitiesChanged(current, capabilities(now));
                        if (added && registration.active) registration.callback.onLinkPropertiesChanged(current, new LinkProperties());
                        if (added && registration.active) registration.callback.onBlockedStatusChanged(current, false);
                    }
                });
            }
        }
        registration.announcedState = matches ? now : 0;
    }

    public static void dispatchNetworkChange() {
        if (nativeTakeChange() == 0) return;
        synchronized (sRegistrations) {
            int state = nativeState();
            for (Registration registration : sRegistrations) update(registration, state);
        }
    }
}
