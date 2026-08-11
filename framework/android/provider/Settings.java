package android.provider;

/**
 * Stub android.provider.Settings.
 *
 * Non-critical for app startup/rendering. Returns defaults so apps don't
 * crash when they query system settings.
 */
public final class Settings {
    private Settings() {
    }

    /**
     * System settings.
     */
    public static final class System {
        /** Screen brightness. */
        public static final String SCREEN_BRIGHTNESS = "screen_brightness";
        /** Screen off timeout. */
        public static final String SCREEN_OFF_TIMEOUT = "screen_off_timeout";
        /** Accelerometer rotation. */
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
     * Global settings.
     */
    public static final class Global {
        /** Airplane mode on. */
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
     * Secure settings.
     */
    public static final class Secure {
        /** Android ID. */
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