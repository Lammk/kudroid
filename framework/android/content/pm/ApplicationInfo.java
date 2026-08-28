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
    public int targetSdkVersion = 29;
    public int flags = 0;
    public int uid = 10000;

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
