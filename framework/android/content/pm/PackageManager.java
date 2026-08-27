package android.content.pm;

/**
 * minimal android.content.pm.packagemanager implementation.
 *
 * provides access to package information. for kudroid minimal framework,
 * most lookups return null/default.
 */
public class PackageManager {
    /** permission granted. */
    public static final int PERMISSION_GRANTED = 0;
    /** permission denied. */
    public static final int PERMISSION_DENIED = -1;

    public PackageManager() {
    }

    /**
     * check if the app has permissions or not. currently returns granted.
     */
    public int checkPermission(String permName, String pkgName) {
        return PERMISSION_GRANTED;
    }

    /**
     * returns application information for a package. currently returns null.
     */
    public ApplicationInfo getApplicationInfo(String packageName, int flags) {
        return new ApplicationInfo();
    }

    /**
     * returns package information for a package. currently returns null.
     */
    public PackageInfo getPackageInfo(String packageName, int flags) {
        return new PackageInfo();
    }

    /**
     * returns the package name that installed this application.
     */
    public String getInstallerPackageName(String packageName) {
        return null;
    }
}
