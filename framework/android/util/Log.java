package android.util;

/**
 * minimal android.util.log implementation.
 *
 * maps to native __android_log_print via a jni native method. this is
 * most used android class — nearly every app and game calls log.d/i/w/e.
 *
 * root side implemented in src/shims/syscallshim.cpp (bionic_android_log_print)
 * and exposed to java through jni registration in kudroid bridge.
 */
public final class Log {
    /** Priority constant for "verbose". */
    public static final int VERBOSE = 2;
    /** Priority constant for "debug". */
    public static final int DEBUG = 3;
    /** Priority constant for "info". */
    public static final int INFO = 4;
    /** Priority constant for "warn". */
    public static final int WARN = 5;
    /** Priority constant for "error". */
    public static final int ERROR = 6;
    /** Priority constant for "assert". */
    public static final int ASSERT = 7;

    private Log() {}

    /**
     * sends a verbose log message.
     */
    public static int v(String tag, String msg) {
        return println_native(VERBOSE, tag, msg);
    }

    /**
     * send a debug log message.
     */
    public static int d(String tag, String msg) {
        return println_native(DEBUG, tag, msg);
    }

    /**
     * send an info log message.
     */
    public static int i(String tag, String msg) {
        return println_native(INFO, tag, msg);
    }

    /**
     * send a warning log message.
     */
    public static int w(String tag, String msg) {
        return println_native(WARN, tag, msg);
    }

    /**
     * sends an error log message.
     */
    public static int e(String tag, String msg) {
        return println_native(ERROR, tag, msg);
    }

    /**
     * send an error log message with a throwable.
     */
    public static int e(String tag, String msg, Throwable tr) {
        String full = msg;
        if (tr != null) {
            full = msg + "\n" + tr.toString();
        }
        return println_native(ERROR, tag, full);
    }

    /**
     * send a warning log message with a throwable.
     */
    public static int w(String tag, String msg, Throwable tr) {
        String full = msg;
        if (tr != null) {
            full = msg + "\n" + tr.toString();
        }
        return println_native(WARN, tag, full);
    }

    /**
     * low-level logging calls. maps to __android_log_print.
     */
    private static native int println_native(int priority, String tag, String msg);
}
