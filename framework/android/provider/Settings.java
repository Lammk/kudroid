package android.provider;

/**
 * mô phỏng android.provider.settings.
 *
 * không quan trọng đối với khởi động/kết xuất ứng dụng. trả về mặc định để các ứng dụng không
 * gặp sự cố khi chúng truy vấn các cài đặt hệ thống.
 */
public final class Settings {
    private Settings() {
    }

    /**
     * các cài đặt hệ thống.
     */
    public static final class System {
        /** độ sáng màn hình. */
        public static final String SCREEN_BRIGHTNESS = "screen_brightness";
        /** thời gian chờ tắt màn hình. */
        public static final String SCREEN_OFF_TIMEOUT = "screen_off_timeout";
        /** xoay gia tốc kế. */
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
     * các cài đặt toàn cầu.
     */
    public static final class Global {
        /** chế độ máy bay bật. */
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
     * các cài đặt an toàn.
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