package android.util;

public final class Slog {
    public static int v(String tag, String msg) { return Log.v(tag, msg); }
    public static int d(String tag, String msg) { return Log.d(tag, msg); }
    public static int i(String tag, String msg) { return Log.i(tag, msg); }
    public static int w(String tag, String msg) { return Log.w(tag, msg); }
    public static int e(String tag, String msg) { return Log.e(tag, msg); }
    public static int wtf(String tag, String msg) { return Log.e(tag, msg); }
    public static int wtf(String tag, Throwable tr) { return Log.e(tag, "WTF", tr); }
    public static int wtf(String tag, String msg, Throwable tr) { return Log.e(tag, msg, tr); }
}
