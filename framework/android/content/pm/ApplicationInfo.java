package android.content.pm;

/**
 * Minimal android.content.pm.ApplicationInfo implementation.
 *
 * Describes an application. For KuDroid's minimal framework, provides basic
 * fields with defaults.
 */
public class ApplicationInfo {
    /** The name of the package this application is. */
    public String packageName = "";
    /** The full path to the base APK for this application. */
    public String sourceDir = "";
    /** The full path to the directory where the APK is installed. */
    public String dataDir = "";
    /** The full path to the directory holding native libraries. */
    public String nativeLibraryDir = "";
    /** The minimum SDK version required. */
    public int minSdkVersion = 0;
    /** The target SDK version. */
    public int targetSdkVersion = 0;
    /** The application's label. */
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
