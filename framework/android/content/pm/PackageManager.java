package android.content.pm;

/**
 * Minimal android.content.pm.PackageManager implementation.
 *
 * Provides access to package information. For KuDroid's minimal framework,
 * most lookups return null/defaults.
 */
public class PackageManager {
    /** Permission granted. */
    public static final int PERMISSION_GRANTED = 0;
    /** Permission denied. */
    public static final int PERMISSION_DENIED = -1;

    public PackageManager() {
    }

    /**
     * Check whether the app has a permission. Returns granted for now.
     */
    public int checkPermission(String permName, String pkgName) {
        return PERMISSION_GRANTED;
    }

    /**
     * Return the application info for a package. Returns null for now.
     */
    public ApplicationInfo getApplicationInfo(String packageName, int flags) {
        return new ApplicationInfo();
    }

    /**
     * Return the package info for a package. Returns null for now.
     */
    public PackageInfo getPackageInfo(String packageName, int flags) {
        return new PackageInfo();
    }

    /**
     * Return the name of the package that installed this app.
     */
    public String getInstallerPackageName(String packageName) {
        return null;
    }
}
