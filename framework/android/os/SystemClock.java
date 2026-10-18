package android.os;

public final class SystemClock {
    private SystemClock() {}
    public static void sleep(long ms) {
        try {
            Thread.sleep(ms);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
    }
    public static boolean setCurrentTimeMillis(long millis) { return false; }
    public static native long uptimeMillis();
    public static native long elapsedRealtime();
    public static native long elapsedRealtimeNanos();
    public static native long currentThreadTimeMillis();
}
