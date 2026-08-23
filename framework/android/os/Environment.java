package android.os;

import java.io.File;

public class Environment {
    public static final String MEDIA_MOUNTED = "mounted";
    public static final String MEDIA_MOUNTED_READ_ONLY = "mounted_ro";
    public static final String MEDIA_UNMOUNTED = "unmounted";

    public static final String DIRECTORY_MUSIC = "Music";
    public static final String DIRECTORY_PODCASTS = "Podcasts";
    public static final String DIRECTORY_RINGTONES = "Ringtones";
    public static final String DIRECTORY_ALARMS = "Alarms";
    public static final String DIRECTORY_NOTIFICATIONS = "Notifications";
    public static final String DIRECTORY_PICTURES = "Pictures";
    public static final String DIRECTORY_MOVIES = "Movies";
    public static final String DIRECTORY_DOWNLOADS = "Download";
    public static final String DIRECTORY_DCIM = "DCIM";
    public static final String DIRECTORY_DOCUMENTS = "Documents";

    public static File getRootDirectory() {
        return new File("/system");
    }

    public static File getDataDirectory() {
        return new File("/data");
    }

    public static File getExternalStorageDirectory() {
        return new File("/sdcard");
    }

    public static File getExternalStoragePublicDirectory(String type) {
        return new File("/sdcard/" + type);
    }

    public static File getDownloadCacheDirectory() {
        return new File("/cache");
    }

    public static String getExternalStorageState() {
        return MEDIA_MOUNTED;
    }

    public static String getExternalStorageState(File path) {
        return MEDIA_MOUNTED;
    }

    public static boolean isExternalStorageRemovable() {
        return false;
    }

    public static boolean isExternalStorageEmulated() {
        return true;
    }
}
