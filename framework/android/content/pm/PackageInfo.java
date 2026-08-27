package android.content.pm;

/**
 * minimal android.content.pm.packageinfo implementation.
 *
 * describes a package. for kudroid minimal framework, provide fields
 * basic with default values.
 */
public class PackageInfo {
    /** package name. */
    public String packageName = "";
    /** version code. */
    public int versionCode = 0;
    /** version name. */
    public String versionName = "";
    /** application information. */
    public ApplicationInfo applicationInfo = new ApplicationInfo();

    public PackageInfo() {
    }
}
