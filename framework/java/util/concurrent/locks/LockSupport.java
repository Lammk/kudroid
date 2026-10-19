package java.util.concurrent.locks;

public class LockSupport {
    private LockSupport() {}
    public static void park(Object blocker) { nativePark(blocker, true, false, 0); }
    public static void park() { nativePark(null, false, false, 0); }
    public static void parkNanos(Object blocker, long nanos) {
        if (nanos > 0) nativePark(blocker, true, false, nanos);
    }
    public static void parkNanos(long nanos) {
        if (nanos > 0) nativePark(null, false, false, nanos);
    }
    public static void parkUntil(Object blocker, long deadline) {
        nativePark(blocker, true, true, deadline);
    }
    public static void parkUntil(long deadline) { nativePark(null, false, true, deadline); }
    public static native void unpark(Thread thread);
    public static native Object getBlocker(Thread t);
    public static native void setCurrentBlocker(Object blocker);
    private static native void nativePark(Object blocker, boolean publishBlocker,
                                          boolean absolute, long time);
}
