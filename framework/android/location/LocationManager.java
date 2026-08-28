package android.location;

import android.os.Looper;
import java.util.List;
import java.util.Collections;

public class LocationManager {
    public static final String NETWORK_PROVIDER = "network";
    public static final String GPS_PROVIDER = "gps";
    public static final String PASSIVE_PROVIDER = "passive";

    public LocationManager() {}
    public List<String> getAllProviders() { return Collections.singletonList(GPS_PROVIDER); }
    public List<String> getProviders(boolean enabledOnly) { return Collections.singletonList(GPS_PROVIDER); }
    public boolean isProviderEnabled(String provider) { return true; }
    public Location getLastKnownLocation(String provider) {
        Location loc = new Location(provider);
        loc.setLatitude(37.4220);
        loc.setLongitude(-122.0841);
        return loc;
    }
    public void requestLocationUpdates(String provider, long minTime, float minDistance, LocationListener listener) {}
    public void requestLocationUpdates(String provider, long minTime, float minDistance, LocationListener listener, Looper looper) {}
    public void removeUpdates(LocationListener listener) {}
}
