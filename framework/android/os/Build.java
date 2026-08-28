package android.os;

public class Build {
    public static final String UNKNOWN = "unknown";
    public static final String ID = "QP1A.191005.007.A1";
    public static final String DISPLAY = "QP1A.191005.007.A1";
    public static final String PRODUCT = "flame";
    public static final String DEVICE = "flame";
    public static final String BOARD = "msmnile";
    public static final String CPU_ABI = "arm64-v8a";
    public static final String CPU_ABI2 = "armeabi-v7a";
    public static final String MANUFACTURER = "Google";
    public static final String BRAND = "google";
    public static final String MODEL = "Pixel 4";
    public static final String BOOTLOADER = "unknown";
    public static final String HARDWARE = "flame";
    public static final String SERIAL = "unknown";
    public static final String TYPE = "user";
    public static final String TAGS = "release-keys";
    public static final String FINGERPRINT = "google/flame/flame:10/QP1A.191005.007.A1/5914597:user/release-keys";
    public static final long TIME = 1570233600000L;
    public static final String USER = "android-build";
    public static final String HOST = "abfarm-release";
    public static final String[] SUPPORTED_ABIS = new String[]{"arm64-v8a", "armeabi-v7a", "armeabi"};
    public static final String[] SUPPORTED_32_BIT_ABIS = new String[]{"armeabi-v7a", "armeabi"};
    public static final String[] SUPPORTED_64_BIT_ABIS = new String[]{"arm64-v8a"};

    public static class VERSION {
        public static final String INCREMENTAL = "5914597";
        public static final String RELEASE = "10";
        public static final String BASE_OS = "";
        public static final String SECURITY_PATCH = "2019-10-05";
        public static final int SDK_INT = 29;
        public static final int PREVIEW_SDK_INT = 0;
        public static final String CODENAME = "REL";
    }

    public static class VERSION_CODES {
        public static final int BASE = 1;
        public static final int BASE_1_1 = 2;
        public static final int CUPCAKE = 3;
        public static final int DONUT = 4;
        public static final int ECLAIR = 5;
        public static final int ECLAIR_0_1 = 6;
        public static final int ECLAIR_MR1 = 7;
        public static final int FROYO = 8;
        public static final int GINGERBREAD = 9;
        public static final int GINGERBREAD_MR1 = 10;
        public static final int HONEYCOMB = 11;
        public static final int HONEYCOMB_MR1 = 12;
        public static final int HONEYCOMB_MR2 = 13;
        public static final int ICE_CREAM_SANDWICH = 14;
        public static final int ICE_CREAM_SANDWICH_MR1 = 15;
        public static final int JELLY_BEAN = 16;
        public static final int JELLY_BEAN_MR1 = 17;
        public static final int JELLY_BEAN_MR2 = 18;
        public static final int KITKAT = 19;
        public static final int KITKAT_WATCH = 20;
        public static final int LOLLIPOP = 21;
        public static final int LOLLIPOP_MR1 = 22;
        public static final int M = 23;
        public static final int N = 24;
        public static final int N_MR1 = 25;
        public static final int O = 26;
        public static final int O_MR1 = 27;
        public static final int P = 28;
        public static final int Q = 29;
    }

    public static String getSerial() { return SERIAL; }
}
