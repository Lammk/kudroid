package android.webkit;

public class GeolocationPermissions {
    public interface Callback {
        void invoke(String origin, boolean allow, boolean retain);
    }

    private static GeolocationPermissions sInstance;

    public static synchronized GeolocationPermissions getInstance() {
        if (sInstance == null) sInstance = new GeolocationPermissions();
        return sInstance;
    }

    public void getOrigins(ValueCallback<java.util.Set<String>> callback) {
        if (callback != null) callback.onReceiveValue(new java.util.HashSet<String>());
    }

    public void getAllowed(String origin, ValueCallback<Boolean> callback) {
        if (callback != null) callback.onReceiveValue(Boolean.FALSE);
    }

    public void allow(String origin) {}
    public void clear(String origin) {}
    public void clearAll() {}
}
