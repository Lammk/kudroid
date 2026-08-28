package android.content.pm;

import android.content.Intent;
import java.util.List;

public abstract class PackageManager {
    public static final int GET_ACTIVITIES = 1;
    public static final int GET_SERVICES = 4;
    public static final int GET_PERMISSIONS = 4096;
    public static final int GET_SIGNATURES = 64;
    public static final int MATCH_DEFAULT_ONLY = 65536;

    public static final int PERMISSION_GRANTED = 0;
    public static final int PERMISSION_DENIED = -1;

    public static final String FEATURE_TOUCHSCREEN = "android.hardware.touchscreen";
    public static final String FEATURE_CAMERA = "android.hardware.camera";
    public static final String FEATURE_WIFI = "android.hardware.wifi";
    public static final String FEATURE_BLUETOOTH = "android.hardware.bluetooth";
    public static final String FEATURE_OPENGL_ES_EXTENSION = "android.hardware.opengles.aep";

    public abstract PackageInfo getPackageInfo(String packageName, int flags) throws NameNotFoundException;
    public abstract ApplicationInfo getApplicationInfo(String packageName, int flags) throws NameNotFoundException;
    public abstract int checkPermission(String permName, String pkgName);
    public abstract boolean hasSystemFeature(String name);
    public abstract boolean hasSystemFeature(String name, int version);
    public abstract ResolveInfo resolveActivity(Intent intent, int flags);
    public abstract List<ResolveInfo> queryIntentActivities(Intent intent, int flags);

    public static class NameNotFoundException extends Exception {
        public NameNotFoundException() { super(); }
        public NameNotFoundException(String name) { super(name); }
    }
}
