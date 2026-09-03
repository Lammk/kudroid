package android.content.pm;

import android.content.ComponentName;
import android.content.Intent;
import java.util.List;

public abstract class PackageManager {
    public static final int GET_ACTIVITIES = 1;
    public static final int GET_SERVICES = 4;
    public static final int GET_PERMISSIONS = 4096;
    public static final int GET_SIGNATURES = 64;
    public static final int MATCH_DEFAULT_ONLY = 65536;

    /**
     * Include &lt;meta-data&gt; in the returned info.
     *
     * The flag a caller passes to getActivityInfo when it wants a manifest key.
     * Its absence is what made AGDK's GameActivity idiom unresolvable, so the whole
     * call auto-stubbed to null:
     *
     *   getActivityInfo(getIntent().getComponent(), PackageManager.GET_META_DATA)
     */
    public static final int GET_META_DATA = 128;

    public static final int PERMISSION_GRANTED = 0;
    public static final int PERMISSION_DENIED = -1;

    public static final String FEATURE_TOUCHSCREEN = "android.hardware.touchscreen";
    public static final String FEATURE_CAMERA = "android.hardware.camera";
    public static final String FEATURE_WIFI = "android.hardware.wifi";
    public static final String FEATURE_BLUETOOTH = "android.hardware.bluetooth";
    public static final String FEATURE_OPENGL_ES_EXTENSION = "android.hardware.opengles.aep";

    // Audio latency features. Read as CONSTANTS by the caller — an app builds
    // hasSystemFeature(FEATURE_AUDIO_LOW_LATENCY) and needs the field to resolve before the
    // query happens, so a missing one is a NoSuchFieldError rather than a false answer.
    //
    // Whether they are actually supported is hasSystemFeature's decision, not this
    // declaration's; see SystemPackageManager, which answers it from the audio path.
    public static final String FEATURE_AUDIO_LOW_LATENCY = "android.hardware.audio.low_latency";
    public static final String FEATURE_AUDIO_OUTPUT = "android.hardware.audio.output";
    public static final String FEATURE_AUDIO_PRO = "android.hardware.audio.pro";
    public static final String FEATURE_MICROPHONE = "android.hardware.microphone";

    public static final String FEATURE_GAMEPAD = "android.hardware.gamepad";
    public static final String FEATURE_SENSOR_ACCELEROMETER =
            "android.hardware.sensor.accelerometer";
    public static final String FEATURE_SENSOR_GYROSCOPE = "android.hardware.sensor.gyroscope";
    public static final String FEATURE_SENSOR_COMPASS = "android.hardware.sensor.compass";
    public static final String FEATURE_TOUCHSCREEN_MULTITOUCH =
            "android.hardware.touchscreen.multitouch";
    public static final String FEATURE_TOUCHSCREEN_MULTITOUCH_DISTINCT =
            "android.hardware.touchscreen.multitouch.distinct";
    public static final String FEATURE_VULKAN_HARDWARE_LEVEL =
            "android.hardware.vulkan.level";
    public static final String FEATURE_VULKAN_HARDWARE_VERSION =
            "android.hardware.vulkan.version";

    public abstract PackageInfo getPackageInfo(String packageName, int flags) throws NameNotFoundException;
    public abstract ApplicationInfo getApplicationInfo(String packageName, int flags) throws NameNotFoundException;

    /** Description of a declared activity, including its manifest meta-data. */
    public abstract ActivityInfo getActivityInfo(ComponentName component, int flags)
            throws NameNotFoundException;

    public abstract ServiceInfo getServiceInfo(ComponentName component, int flags)
            throws NameNotFoundException;

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
