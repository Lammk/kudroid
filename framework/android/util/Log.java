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
     * Whether a message at this level would be logged.
     *
     * Libraries gate expensive work on this — building a string, serialising an
     * object — so it must exist or that guard throws. KuDroid forwards everything to
     * stderr and drops nothing, so the honest answer for anything at DEBUG or above
     * is true. VERBOSE is excluded to match Android's default, where it is off unless
     * explicitly enabled; claiming it is on would make chatty libraries flood the log.
     */
    public static boolean isLoggable(String tag, int level) {
        return level >= DEBUG;
    }

    /**
     * Format a Throwable the way Log does, for callers that log it themselves.
     */
    public static String getStackTraceString(Throwable tr) {
        if (tr == null) return "";
        StringBuilder sb = new StringBuilder();
        sb.append(tr.toString());
        StackTraceElement[] trace = tr.getStackTrace();
        if (trace != null) {
            for (StackTraceElement e : trace) {
                sb.append("\n\tat ").append(e.toString());
            }
        }
        for (Throwable c = tr.getCause(); c != null; c = c.getCause()) {
            sb.append("\nCaused by: ").append(c.toString());
            if (c.getCause() == c) break;
        }
        return sb.toString();
    }

    public static int println(int priority, String tag, String msg) {
        return println_native(priority, tag, msg);
    }

    public static int wtf(String tag, String msg) {
        return println_native(ASSERT, tag, msg);
    }

    public static int wtf(String tag, Throwable tr) {
        return println_native(ASSERT, tag, getStackTraceString(tr));
    }

    public static int wtf(String tag, String msg, Throwable tr) {
        return println_native(ASSERT, tag, msg + "\n" + getStackTraceString(tr));
    }

    /**
     * low-level logging calls. maps to __android_log_print.
     */
    private static native int println_native(int priority, String tag, String msg);
}
