package android.location;

/**
 * mô phỏng android.location.locationmanager.
 *
 * không quan trọng đối với khởi động/kết xuất ứng dụng. trả về rỗng/mặc định để các ứng dụng không
 * gặp sự cố khi chúng truy vấn vị trí.
 */
public class LocationManager {
    /** nhà cung cấp gps. */
    public static final String GPS_PROVIDER = "gps";
    /** nhà cung cấp mạng. */
    public static final String NETWORK_PROVIDER = "network";
    /** nhà cung cấp thụ động. */
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
     * trình nghe cho các cập nhật vị trí.
     */
    public interface LocationListener {
        void onLocationChanged(Location location);
        void onStatusChanged(String provider, int status, android.os.Bundle extras);
        void onProviderEnabled(String provider);
        void onProviderDisabled(String provider);
    }
}