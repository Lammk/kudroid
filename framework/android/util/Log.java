package android.util;

/**
 * Minimal android.util.Log implementation.
 *
 * Maps to the native __android_log_print via a JNI native method. This is the
 * single most-used Android class — nearly every app and game calls Log.d/i/w/e.
 *
 * The native side is implemented in src/shims/SyscallShim.cpp (bionic_android_log_print)
 * and exposed to Java through a JNI registration in the KuDroid bridge.
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
     * Send a verbose log message.
     */
    public static int v(String tag, String msg) {
        return println_native(VERBOSE, tag, msg);
    }

    /**
     * Send a debug log message.
     */
    public static int d(String tag, String msg) {
        return println_native(DEBUG, tag, msg);
    }

    /**
     * Send an info log message.
     */
    public static int i(String tag, String msg) {
        return println_native(INFO, tag, msg);
    }

    /**
     * Send a warning log message.
     */
    public static int w(String tag, String msg) {
        return println_native(WARN, tag, msg);
    }

    /**
     * Send an error log message.
     */
    public static int e(String tag, String msg) {
        return println_native(ERROR, tag, msg);
    }

    /**
     * Send an error log message with a throwable.
     */
    public static int e(String tag, String msg, Throwable tr) {
        String full = msg;
        if (tr != null) {
            full = msg + "\n" + tr.toString();
        }
        return println_native(ERROR, tag, full);
    }

    /**
     * Send a warning log message with a throwable.
     */
    public static int w(String tag, String msg, Throwable tr) {
        String full = msg;
        if (tr != null) {
            full = msg + "\n" + tr.toString();
        }
        return println_native(WARN, tag, full);
    }

    /**
     * Low-level logging call. Maps to __android_log_print.
     */
    private static native int println_native(int priority, String tag, String msg);
}
