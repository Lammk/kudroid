package android.provider;

import android.content.ContentResolver;

public final class Settings {
    public static final class System {
        public static final String ANDROID_ID = "android_id";
        public static String getString(ContentResolver resolver, String name) {
            if (ANDROID_ID.equals(name)) return "9774d56d682e549c";
            return "";
        }
        public static int getInt(ContentResolver cr, String name, int def) { return def; }
        public static long getLong(ContentResolver cr, String name, long def) { return def; }
        public static float getFloat(ContentResolver cr, String name, float def) { return def; }
    }

    public static final class Secure {
        public static final String ANDROID_ID = "android_id";
        public static String getString(ContentResolver resolver, String name) {
            if (ANDROID_ID.equals(name)) return "9774d56d682e549c";
            return "";
        }
        public static int getInt(ContentResolver cr, String name, int def) { return def; }
        public static long getLong(ContentResolver cr, String name, long def) { return def; }
        public static float getFloat(ContentResolver cr, String name, float def) { return def; }
    }

    public static final class Global {
        public static String getString(ContentResolver resolver, String name) { return ""; }
        public static int getInt(ContentResolver cr, String name, int def) { return def; }
        public static long getLong(ContentResolver cr, String name, long def) { return def; }
        public static float getFloat(ContentResolver cr, String name, float def) { return def; }
    }
}
