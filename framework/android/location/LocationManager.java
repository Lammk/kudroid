package android.location;

import android.os.CancellationSignal;
import android.os.Handler;
import android.os.Looper;
import java.util.ArrayList;
import java.util.Collections;
import java.util.IdentityHashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.Executor;
import java.util.concurrent.atomic.AtomicLong;
import java.util.function.Consumer;

/**
 * LocationManager backed by the host location service (CoreLocation on
 * iOS) through the LocationHost bridge
 * (include/kudroid/platform/LocationHost.h).
 *
 * Fixes are drained by a poll loop on the main Looper and re-posted to each
 * listener's own Looper, mirroring Android's delivery semantics. There is
 * exactly one physical provider on the host; "gps" and "network" both map
 * to it, which matches how hybrid runtimes (e.g. Tauri's geolocation
 * plugin) consume this API.
 */
public class LocationManager {
    public static final String NETWORK_PROVIDER = "network";
    public static final String GPS_PROVIDER = "gps";
    public static final String PASSIVE_PROVIDER = "passive";
    public static final String FUSED_PROVIDER = "fused";
    public static final String KEY_PROXIMITY_ENTERING = "entering";
    public static final String KEY_PROVIDER_ENABLED = "providerEnabled";
    public static final String KEY_STATUS_CHANGED = "statusChanged";
    public static final String KEY_LOCATION_CHANGED = "locationChanged";
    public static final int OUT_OF_SERVICE = 0;
    public static final int TEMPORARILY_UNAVAILABLE = 1;
    public static final int AVAILABLE = 2;

    private static final AtomicLong NEXT_HANDLE = new AtomicLong(1);

    private static final class Subscription {
        long handle;
        String provider;
        LocationListener listener;
        Looper looper;
        boolean single;
    }

    private final Map<LocationListener, Subscription> mSubs = new IdentityHashMap<>();
    private final Handler mMainHandler = new Handler(Looper.getMainLooper());
    private boolean mPolling = false;

    private final Runnable mPollTask = new Runnable() {
        @Override public void run() {
            List<Subscription> snapshot;
            synchronized (mSubs) {
                snapshot = new ArrayList<>(mSubs.values());
            }
            for (Subscription sub : snapshot) {
                String encoded;
                while ((encoded = nativeLocPoll(sub.handle)) != null) {
                    deliver(sub, parseFix(sub.provider, encoded));
                    if (sub.single) {
                        stopSubscription(sub);
                        break;
                    }
                }
            }
            synchronized (mSubs) {
                if (!mSubs.isEmpty()) {
                    mMainHandler.postDelayed(this, 200);
                } else {
                    mPolling = false;
                }
            }
        }
    };

    public LocationManager() {}

    public List<String> getAllProviders() {
        List<String> providers = new ArrayList<>();
        providers.add(GPS_PROVIDER);
        providers.add(NETWORK_PROVIDER);
        providers.add(PASSIVE_PROVIDER);
        return providers;
    }

    public List<String> getProviders(boolean enabledOnly) {
        if (enabledOnly && !nativeLocEnabled()) return Collections.emptyList();
        return getAllProviders();
    }

    public List<String> getProviders(Criteria criteria, boolean enabledOnly) {
        return getProviders(enabledOnly);
    }

    public String getBestProvider(Criteria criteria, boolean enabledOnly) {
        return GPS_PROVIDER;
    }

    public boolean isProviderEnabled(String provider) {
        return nativeLocEnabled();
    }

    public boolean isLocationEnabled() {
        return nativeLocEnabled();
    }

    public Location getLastKnownLocation(String provider) {
        String encoded = nativeLocLastKnown();
        if (encoded == null) return null;
        return parseFix(provider, encoded);
    }

    public void requestLocationUpdates(String provider, long minTime, float minDistance,
                                       LocationListener listener) {
        requestLocationUpdates(provider, minTime, minDistance, listener,
                               Looper.myLooper() != null ? Looper.myLooper()
                                                         : Looper.getMainLooper());
    }

    public void requestLocationUpdates(String provider, long minTime, float minDistance,
                                       LocationListener listener, Looper looper) {
        if (listener == null) return;
        Subscription sub = new Subscription();
        sub.handle = NEXT_HANDLE.getAndIncrement();
        sub.provider = provider;
        sub.listener = listener;
        sub.looper = looper;
        sub.single = false;
        synchronized (mSubs) {
            Subscription old = mSubs.put(listener, sub);
            if (old != null) nativeLocStop(old.handle);
        }
        nativeLocStart(sub.handle, minTime, minDistance);
        ensurePolling();
    }

    public void requestSingleUpdate(String provider, LocationListener listener, Looper looper) {
        if (listener == null) return;
        Subscription sub = new Subscription();
        sub.handle = NEXT_HANDLE.getAndIncrement();
        sub.provider = provider;
        sub.listener = listener;
        sub.looper = looper != null ? looper : Looper.getMainLooper();
        sub.single = true;
        synchronized (mSubs) {
            mSubs.put(listener, sub);
        }
        nativeLocSingle(sub.handle);
        ensurePolling();
    }

    public void getCurrentLocation(String provider, CancellationSignal cancellationSignal,
                                   Executor executor, Consumer<Location> consumer) {
        if (consumer == null) return;
        Executor exec = executor != null ? executor : mMainHandler::post;
        final boolean[] delivered = {false};
        LocationListener once = new LocationListener() {
            @Override public void onLocationChanged(Location location) {
                synchronized (delivered) {
                    if (delivered[0]) return;
                    delivered[0] = true;
                }
                exec.execute(() -> consumer.accept(location));
            }
            @Override public void onStatusChanged(String p, int s, android.os.Bundle e) {}
            @Override public void onProviderEnabled(String p) {}
            @Override public void onProviderDisabled(String p) {}
        };
        requestSingleUpdate(provider, once, Looper.getMainLooper());
        if (cancellationSignal != null && cancellationSignal.isCanceled()) {
            removeUpdates(once);
        }
    }

    public void removeUpdates(LocationListener listener) {
        Subscription sub;
        synchronized (mSubs) {
            sub = mSubs.remove(listener);
        }
        if (sub != null) nativeLocStop(sub.handle);
    }

    // -------------------------------------------------------------- internals

    private void ensurePolling() {
        synchronized (mSubs) {
            if (mPolling) return;
            mPolling = true;
        }
        mMainHandler.post(mPollTask);
    }

    private void stopSubscription(Subscription sub) {
        synchronized (mSubs) {
            if (mSubs.get(sub.listener) == sub) mSubs.remove(sub.listener);
        }
        nativeLocStop(sub.handle);
    }

    private void deliver(Subscription sub, Location location) {
        if (location == null) return;
        if (sub.looper == Looper.getMainLooper()
                || Looper.myLooper() == sub.looper) {
            sub.listener.onLocationChanged(location);
        } else {
            new Handler(sub.looper).post(() -> sub.listener.onLocationChanged(location));
        }
    }

    // Wire format from LocationHost::Encode:
    // "<lat>|<lon>|<accuracy>|<altitude>|<speed>|<bearing>|<timeMs>"
    static Location parseFix(String provider, String encoded) {
        if (encoded == null) return null;
        String[] p = encoded.split("\\|", -1);
        if (p.length < 7) return null;
        try {
            Location loc = new Location(provider != null ? provider : GPS_PROVIDER);
            loc.setLatitude(Double.parseDouble(p[0]));
            loc.setLongitude(Double.parseDouble(p[1]));
            loc.setAccuracy(Float.parseFloat(p[2]));
            loc.setAltitude(Double.parseDouble(p[3]));
            loc.setSpeed(Float.parseFloat(p[4]));
            loc.setBearing(Float.parseFloat(p[5]));
            loc.setTime(Long.parseLong(p[6]));
            return loc;
        } catch (NumberFormatException e) {
            return null;
        }
    }

    // --------------------------------------------------------------- natives

    private static native void nativeLocStart(long handle, long minTimeMs, float minDistanceM);
    private static native void nativeLocStop(long handle);
    private static native void nativeLocSingle(long handle);
    private static native boolean nativeLocEnabled();
    private static native String nativeLocPoll(long handle);
    private static native String nativeLocLastKnown();
}
