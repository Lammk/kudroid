package android.location;

/**
 * emulate android.location.locationmanager.
 *
 * is not important for application startup/rendering. Returns null/default to no applications
 * crashes when they query for location.
 */
public class LocationManager {
    /** gps provider. */
    public static final String GPS_PROVIDER = "gps";
    /** network provider. */
    public static final String NETWORK_PROVIDER = "network";
    /** passive provider. */
    public static final String PASSIVE_PROVIDER = "passive";

    public LocationManager() {
    }

    public Location getLastKnownLocation(String provider) {
        return null;
    }

    public boolean isProviderEnabled(String provider) {
        return false;
    }

    public boolean isLocationEnabled() {
        return false;
    }

    public java.util.List<String> getProviders(boolean enabledOnly) {
        return new java.util.ArrayList<String>();
    }

    public String getBestProvider(Criteria criteria, boolean enabledOnly) {
        return null;
    }

    public void requestLocationUpdates(String provider, long minTime, float minDistance,
                                       LocationListener listener) {
    }

    public void removeUpdates(LocationListener listener) {
    }

    /**
     * listener for location updates.
     */
    public interface LocationListener {
        void onLocationChanged(Location location);
        void onStatusChanged(String provider, int status, android.os.Bundle extras);
        void onProviderEnabled(String provider);
        void onProviderDisabled(String provider);
    }
}