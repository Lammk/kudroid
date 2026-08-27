package android.content.pm;

/**
 * minimal android.content.pm.applicationinfo implementation.
 *
 * describes an application. for kudroid minimal framework, provide fields
 * basic with default values.
 */
public class ApplicationInfo {
    /** package name of this application. */
    public String packageName = "";
    /** full path to the base apk for this app. */
    public String sourceDir = "";
    /** full path to the directory where the apk is installed. */
    public String dataDir = "";
    /** full path to the directory containing the original libraries. */
    public String nativeLibraryDir = "";
    /** Minimum required sdk version. */
    public int minSdkVersion = 0;
    /** target sdk version. */
    public int targetSdkVersion = 0;
    /** application label. */
    public CharSequence loadLabel = "";

    public ApplicationInfo() {
    }

    public ApplicationInfo(ApplicationInfo orig) {
        packageName = orig.packageName;
        sourceDir = orig.sourceDir;
        dataDir = orig.dataDir;
        nativeLibraryDir = orig.nativeLibraryDir;
        minSdkVersion = orig.minSdkVersion;
        targetSdkVersion = orig.targetSdkVersion;
        loadLabel = orig.loadLabel;
    }
}
