package android.os;

public final class SystemClock {
    private SystemClock() {}
    public static void sleep(long ms) {
        try {
            Thread.sleep(ms);
        } catch (InterruptedException e) {}
    }
    public static boolean setCurrentTimeMillis(long millis) { return false; }
    public static long uptimeMillis() { return System.currentTimeMillis(); }
    public static long elapsedRealtime() { return System.currentTimeMillis(); }
    public static long elapsedRealtimeNanos() { return System.nanoTime(); }
    public static long currentThreadTimeMillis() { return System.currentTimeMillis(); }
}
