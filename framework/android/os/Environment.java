package android.os;

import java.io.File;

public class Environment {
    public static final String MEDIA_UNKNOWN = "unknown";
    public static final String MEDIA_REMOVED = "removed";
    public static final String MEDIA_UNMOUNTED = "unmounted";
    public static final String MEDIA_CHECKING = "checking";
    public static final String MEDIA_NOFS = "nofs";
    public static final String MEDIA_MOUNTED = "mounted";
    public static final String MEDIA_MOUNTED_READ_ONLY = "mounted_ro";
    public static final String MEDIA_SHARED = "shared";
    public static final String MEDIA_BAD_REMOVAL = "bad_removal";
    public static final String MEDIA_UNMOUNTABLE = "unmountable";
    public static final String MEDIA_EJECTING = "ejecting";

    public static String DIRECTORY_MUSIC = "Music";
    public static String DIRECTORY_PODCASTS = "Podcasts";
    public static String DIRECTORY_RINGTONES = "Ringtones";
    public static String DIRECTORY_ALARMS = "Alarms";
    public static String DIRECTORY_NOTIFICATIONS = "Notifications";
    public static String DIRECTORY_PICTURES = "Pictures";
    public static String DIRECTORY_MOVIES = "Movies";
    public static String DIRECTORY_DOWNLOADS = "Download";
    public static String DIRECTORY_DCIM = "DCIM";
    public static String DIRECTORY_DOCUMENTS = "Documents";

    public static File getRootDirectory() { return new File("/system"); }
    public static File getDataDirectory() { return new File("/data"); }
    public static File getExternalStorageDirectory() { return new File("/sdcard"); }
    public static File getExternalStoragePublicDirectory(String type) { return new File("/sdcard/" + type); }
    public static File getDownloadCacheDirectory() { return new File("/cache"); }
    public static String getExternalStorageState() { return MEDIA_MOUNTED; }
    public static String getExternalStorageState(File path) { return MEDIA_MOUNTED; }
    public static boolean isExternalStorageRemovable() { return false; }
    public static boolean isExternalStorageEmulated() { return true; }
}
