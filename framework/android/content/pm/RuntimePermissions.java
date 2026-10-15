package android.content.pm;

/**
 * Runtime permissions whose use depends on the host OS granting the sensor.
 *
 * The microphone and camera need both the app's own grant and the host's authorization;
 * every other permission is reported granted.
 */
public final class RuntimePermissions {
    public static final String CAMERA = "android.permission.CAMERA";
    public static final String RECORD_AUDIO = "android.permission.RECORD_AUDIO";

    private RuntimePermissions() {}

    private static native boolean isGrantedNative(String packageName, String permission);

    public static int check(String packageName, String permission) {
        if (permission == null) return PackageManager.PERMISSION_DENIED;
        if (!CAMERA.equals(permission) && !RECORD_AUDIO.equals(permission)) {
            return PackageManager.PERMISSION_GRANTED;
        }
        return isGrantedNative(packageName != null ? packageName : "", permission)
                ? PackageManager.PERMISSION_GRANTED : PackageManager.PERMISSION_DENIED;
    }

    public static boolean isGranted(String packageName, String permission) {
        return check(packageName, permission) == PackageManager.PERMISSION_GRANTED;
    }
}
