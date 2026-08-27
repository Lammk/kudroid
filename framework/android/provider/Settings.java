package android.provider;

/**
 * simulate android.provider.settings.
 *
 * is not important for application startup/rendering. Returns default to no applications
 * crashes when they query system settings.
 */
public final class Settings {
    private Settings() {
    }

    /**
     * system settings.
     */
    public static final class System {
        /** screen brightness. */
        public static final String SCREEN_BRIGHTNESS = "screen_brightness";
        /** screen off timeout. */
        public static final String SCREEN_OFF_TIMEOUT = "screen_off_timeout";
        /** rotate the accelerometer. */
        public static final String ACCELEROMETER_ROTATION = "accelerometer_rotation";

        private System() {
        }

        public static int getInt(android.content.ContentResolver cr, String name) {
            return 0;
        }

        public static int getInt(android.content.ContentResolver cr, String name, int def) {
            return def;
        }

        public static boolean putInt(android.content.ContentResolver cr, String name, int value) {
            return false;
        }

        public static long getLong(android.content.ContentResolver cr, String name) {
            return 0L;
        }

        public static long getLong(android.content.ContentResolver cr, String name, long def) {
            return def;
        }

        public static boolean putLong(android.content.ContentResolver cr, String name, long value) {
            return false;
        }

        public static float getFloat(android.content.ContentResolver cr, String name) {
            return 0.0f;
        }

        public static float getFloat(android.content.ContentResolver cr, String name, float def) {
            return def;
        }

        public static boolean putFloat(android.content.ContentResolver cr, String name, float value) {
            return false;
        }

        public static String getString(android.content.ContentResolver cr, String name) {
            return null;
        }

        public static boolean putString(android.content.ContentResolver cr, String name, String value) {
            return false;
        }
    }

    /**
     * global settings.
     */
    public static final class Global {
        /** airplane mode on. */
        public static final String AIRPLANE_MODE_ON = "airplane_mode_on";

        private Global() {
        }

        public static int getInt(android.content.ContentResolver cr, String name) {
            return 0;
        }

        public static int getInt(android.content.ContentResolver cr, String name, int def) {
            return def;
        }

        public static boolean putInt(android.content.ContentResolver cr, String name, int value) {
            return false;
        }

        public static String getString(android.content.ContentResolver cr, String name) {
            return null;
        }

        public static boolean putString(android.content.ContentResolver cr, String name, String value) {
            return false;
        }
    }

    /**
     * safety settings.
     */
    public static final class Secure {
        /** id android. */
        public static final String ANDROID_ID = "android_id";

        private Secure() {
        }

        public static int getInt(android.content.ContentResolver cr, String name) {
            return 0;
        }

        public static int getInt(android.content.ContentResolver cr, String name, int def) {
            return def;
        }

        public static boolean putInt(android.content.ContentResolver cr, String name, int value) {
            return false;
        }

        public static String getString(android.content.ContentResolver cr, String name) {
            return null;
        }

        public static boolean putString(android.content.ContentResolver cr, String name, String value) {
            return false;
        }
    }
}