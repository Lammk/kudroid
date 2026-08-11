package android.util;

/**
 * triển khai android.util.log tối thiểu.
 *
 * ánh xạ tới __android_log_print gốc thông qua một phương thức gốc jni. đây là
 * lớp android được sử dụng nhiều nhất — gần như mọi ứng dụng và trò chơi đều gọi log.d/i/w/e.
 *
 * phía gốc được triển khai trong src/shims/syscallshim.cpp (bionic_android_log_print)
 * và tiếp xúc với java thông qua đăng ký jni trong cầu nối kudroid.
 */
public final class Log {
    /** hằng số ưu tiên cho "verbose". */
    public static final int VERBOSE = 2;
    /** hằng số ưu tiên cho "debug". */
    public static final int DEBUG = 3;
    /** hằng số ưu tiên cho "info". */
    public static final int INFO = 4;
    /** hằng số ưu tiên cho "warn". */
    public static final int WARN = 5;
    /** hằng số ưu tiên cho "error". */
    public static final int ERROR = 6;
    /** hằng số ưu tiên cho "assert". */
    public static final int ASSERT = 7;

    private Log() {}

    /**
     * gửi một thông báo nhật ký verbose.
     */
    public static int v(String tag, String msg) {
        return println_native(VERBOSE, tag, msg);
    }

    /**
     * gửi một thông báo nhật ký debug.
     */
    public static int d(String tag, String msg) {
        return println_native(DEBUG, tag, msg);
    }

    /**
     * gửi một thông báo nhật ký info.
     */
    public static int i(String tag, String msg) {
        return println_native(INFO, tag, msg);
    }

    /**
     * gửi một thông báo nhật ký warning.
     */
    public static int w(String tag, String msg) {
        return println_native(WARN, tag, msg);
    }

    /**
     * gửi một thông báo nhật ký error.
     */
    public static int e(String tag, String msg) {
        return println_native(ERROR, tag, msg);
    }

    /**
     * gửi một thông báo nhật ký error với một throwable.
     */
    public static int e(String tag, String msg, Throwable tr) {
        String full = msg;
        if (tr != null) {
            full = msg + "\n" + tr.toString();
        }
        return println_native(ERROR, tag, full);
    }

    /**
     * gửi một thông báo nhật ký warning với một throwable.
     */
    public static int w(String tag, String msg, Throwable tr) {
        String full = msg;
        if (tr != null) {
            full = msg + "\n" + tr.toString();
        }
        return println_native(WARN, tag, full);
    }

    /**
     * lệnh gọi ghi nhật ký cấp thấp. ánh xạ tới __android_log_print.
     */
    private static native int println_native(int priority, String tag, String msg);
}
