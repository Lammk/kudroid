package android.provider;

import android.content.ContentResolver;
import android.net.Uri;

public final class Settings {
    private static String sAndroidId;

    private static synchronized String androidId() {
        if (sAndroidId == null) {
            try {
                sAndroidId = nativeGetAndroidId();
            } catch (UnsatisfiedLinkError e) {
                sAndroidId = null;
            }
        }
        return sAndroidId;
    }

    private static native String nativeGetAndroidId();

    public static final class System {
        public static final Uri CONTENT_URI = Uri.parse("content://settings/system");
        public static final String ANDROID_ID = "android_id";
        public static String getString(ContentResolver resolver, String name) {
            if (ANDROID_ID.equals(name)) {
                String id = androidId();
                return id != null ? id : "";
            }
            return "";
        }
        public static int getInt(ContentResolver cr, String name, int def) { return def; }
        public static long getLong(ContentResolver cr, String name, long def) { return def; }
        public static float getFloat(ContentResolver cr, String name, float def) { return def; }
        public static Uri getUriFor(String name) {
            return Uri.parse("content://settings/system/" + name);
        }
    }

    public static final class Secure {
        public static final Uri CONTENT_URI = Uri.parse("content://settings/secure");
        public static final String ANDROID_ID = "android_id";
        public static String getString(ContentResolver resolver, String name) {
            if (ANDROID_ID.equals(name)) {
                String id = androidId();
                return id != null ? id : "";
            }
            return "";
        }
        public static int getInt(ContentResolver cr, String name, int def) { return def; }
        public static long getLong(ContentResolver cr, String name, long def) { return def; }
        public static float getFloat(ContentResolver cr, String name, float def) { return def; }
        public static Uri getUriFor(String name) {
            return Uri.parse("content://settings/secure/" + name);
        }
    }

    public static final class Global {
        public static final Uri CONTENT_URI = Uri.parse("content://settings/global");
        public static String getString(ContentResolver resolver, String name) { return ""; }
        public static int getInt(ContentResolver cr, String name, int def) { return def; }
        public static long getLong(ContentResolver cr, String name, long def) { return def; }
        public static float getFloat(ContentResolver cr, String name, float def) { return def; }
        public static Uri getUriFor(String name) {
            return Uri.parse("content://settings/global/" + name);
        }
    }
}
