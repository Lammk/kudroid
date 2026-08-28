package android.content.pm;

import android.content.Intent;
import java.util.List;
import java.util.Collections;

public class SystemPackageManager extends PackageManager {
    public PackageInfo getPackageInfo(String packageName, int flags) throws NameNotFoundException {
        PackageInfo pi = new PackageInfo();
        pi.packageName = packageName;
        pi.applicationInfo = getApplicationInfo(packageName, flags);
        return pi;
    }
    public ApplicationInfo getApplicationInfo(String packageName, int flags) throws NameNotFoundException {
        ApplicationInfo ai = new ApplicationInfo();
        ai.packageName = packageName;
        ai.dataDir = "/data/data/" + packageName;
        ai.sourceDir = "/data/app/" + packageName + "/base.apk";
        ai.nativeLibraryDir = "/data/app/" + packageName + "/lib/arm64-v8a";
        return ai;
    }
    public int checkPermission(String permName, String pkgName) { return PERMISSION_GRANTED; }
    public boolean hasSystemFeature(String name) { return true; }
    public boolean hasSystemFeature(String name, int version) { return true; }
    public ResolveInfo resolveActivity(Intent intent, int flags) { return new ResolveInfo(); }
    public List<ResolveInfo> queryIntentActivities(Intent intent, int flags) { return Collections.emptyList(); }
}
