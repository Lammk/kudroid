package android.os;

/**
 * The device KuDroid reports itself as.
 *
 * Values are kept in step with include/kudroid/DeviceProfile.h, which the C++ side
 * uses for the property service and /system/build.prop. Java and native code must
 * agree: an app that reads Build.VERSION.SDK_INT and a library that reads
 * ro.build.version.sdk are asking the same question, and they used to get different
 * answers (29 here, 34 in build.prop).
 *
 * This used to claim to be a Pixel 4 (google/flame, Android 10). Spoofing real
 * hardware invites an app down a device-specific path — vendor GPU workarounds,
 * Play-services assumptions, per-model quirk tables — that KuDroid cannot honour, and
 * it made every bug report read as if it came from a Pixel. Reporting KuDroid is
 * honest and no less functional: apps that gate on API level use SDK_INT, which is
 * unchanged.
 */
public class Build {
    public static final String UNKNOWN = "unknown";
    public static final String ID = "QP1A.190711.020";
    public static final String DISPLAY = "QP1A.190711.020";
    public static final String PRODUCT = "kudroid";
    public static final String DEVICE = "kudroid_arm64";
    public static final String BOARD = "kudroid";
    public static final String CPU_ABI = "arm64-v8a";
    /**
     * Empty, not "armeabi-v7a".
     *
     * KuDroid's ELF loader only relocates AArch64 (R_AARCH64_*), so a 32-bit library
     * cannot be mapped. Advertising armeabi-v7a here invites an app to choose the
     * 32-bit half of a fat native library set and then fail to load it — a failure
     * that looks like a missing library rather than an unsupported architecture.
     */
    public static final String CPU_ABI2 = "";
    public static final String MANUFACTURER = "KuDroid";
    public static final String BRAND = "kudroid";
    public static final String MODEL = "KuDroid";
    public static final String BOOTLOADER = "unknown";
    public static final String HARDWARE = "kudroid";
    public static final String SERIAL = "unknown";
    public static final String TYPE = "user";
    public static final String TAGS = "release-keys";
    public static final String FINGERPRINT =
            "kudroid/kudroid/kudroid_arm64:10/QP1A.190711.020/6000000:user/release-keys";
    public static final long TIME = 1570233600000L;
    public static final String USER = "android-build";
    public static final String HOST = "kudroid";

    /** 64-bit only, for the reason given on {@link #CPU_ABI2}. */
    public static final String[] SUPPORTED_ABIS = new String[]{"arm64-v8a"};
    public static final String[] SUPPORTED_32_BIT_ABIS = new String[0];
    public static final String[] SUPPORTED_64_BIT_ABIS = new String[]{"arm64-v8a"};

    public static class VERSION {
        public static final String INCREMENTAL = "6000000";
        public static final String RELEASE = "10";
        /**
         * Since API 30 this is what apps are told to read; on older platforms it is
         * the same string as RELEASE.
         */
        public static final String RELEASE_OR_CODENAME = "10";
        public static final String BASE_OS = "";
        public static final String SECURITY_PATCH = "2019-10-05";
        /**
         * The platform level KuART's behaviour was ported from. Raising it means
         * auditing the framework for the APIs each level adds, not editing this
         * number — an app that sees a higher level will call methods that do not
         * exist yet.
         */
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
        // Levels above what KuDroid reports still have to EXIST as constants. Apps
        // and androidx compare SDK_INT against them — `if (SDK_INT >= VERSION_CODES.S)`
        // is the standard shape — and a missing static field is a NoSuchFieldError at
        // the comparison, not a false branch. The comparison correctly yields false.
        public static final int R = 30;
        public static final int S = 31;
        public static final int S_V2 = 32;
        public static final int TIRAMISU = 33;
        public static final int UPSIDE_DOWN_CAKE = 34;
        public static final int VANILLA_ICE_CREAM = 35;
        public static final int CUR_DEVELOPMENT = 10000;
    }

    public static String getSerial() { return SERIAL; }

    /** @return the radio firmware version; KuDroid has no radio. */
    public static String getRadioVersion() { return ""; }
}
