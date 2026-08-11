package android.content.pm;

/**
 * Minimal android.content.pm.PackageInfo implementation.
 *
 * Describes a package. For KuDroid's minimal framework, provides basic fields
 * with defaults.
 */
public class PackageInfo {
    /** The name of the package. */
    public String packageName = "";
    /** The version code. */
    public int versionCode = 0;
    /** The version name. */
    public String versionName = "";
    /** The application info. */
    public ApplicationInfo applicationInfo = new ApplicationInfo();

    public PackageInfo() {
    }
}
