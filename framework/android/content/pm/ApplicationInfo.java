package android.content.pm;

import android.os.Parcel;
import android.os.Parcelable;

public class ApplicationInfo implements Parcelable {
    public String name;
    public String packageName;
    public String className;
    public String sourceDir;
    public String publicSourceDir;
    public String dataDir;
    public String nativeLibraryDir;
    /**
     * What the app declared it was built against.
     *
     * Defaults to the platform level because the manifest parser does not read
     * android:targetSdkVersion yet, and the platform level is the closest safe guess:
     * a value BELOW the real one makes Android enable legacy-compatibility behaviour,
     * which is the wrong default for a modern app. Not the same question as
     * Build.VERSION.SDK_INT, which is what the device runs — an app comparing the two
     * is asking whether it is running on a newer platform than it was built for.
     */
    public int targetSdkVersion = android.os.Build.VERSION.SDK_INT;
    public int minSdkVersion = 21;
    public int compileSdkVersion = android.os.Build.VERSION.SDK_INT;
    public int flags = 0;
    public int uid = 10000;

    // Read by apps inspecting their own ApplicationInfo, and previously absent.
    /** Whether the application is enabled. */
    public boolean enabled = true;
    /** Icon, label and logo resources; 0 when the manifest set none. */
    public int icon;
    public int labelRes;
    public int logo;
    public int theme;
    public int descriptionRes;
    /** Meta-data from the manifest; never null so callers can index it. */
    public android.os.Bundle metaData = new android.os.Bundle();
    public String processName;
    public String taskAffinity;
    public String permission;
    public String backupAgentName;
    public String[] splitSourceDirs;
    public String[] splitPublicSourceDirs;
    public String[] sharedLibraryFiles;
    public CharSequence nonLocalizedLabel;
    public String deviceProtectedDataDir;
    public String credentialProtectedDataDir;

    public static final int FLAG_SYSTEM = 1<<0;
    public static final int FLAG_DEBUGGABLE = 1<<1;
    public static final int FLAG_ALLOW_BACKUP = 1<<10;
    public static final int FLAG_LARGE_HEAP = 1<<20;
    public static final int FLAG_SUPPORTS_RTL = 1<<22;

    public ApplicationInfo() {}
    public CharSequence loadLabel(PackageManager pm) { return packageName; }
    public int describeContents() { return 0; }
    public void writeToParcel(Parcel dest, int flags) {}
}
