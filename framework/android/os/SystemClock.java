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
    // Monotonic: wall-clock jumps (NTP, user) must not move timers.
    public static long uptimeMillis() { return System.nanoTime() / 1000000L; }
    public static long elapsedRealtime() { return System.nanoTime() / 1000000L; }
    public static long elapsedRealtimeNanos() { return System.nanoTime(); }
    public static long currentThreadTimeMillis() { return System.nanoTime() / 1000000L; }
}
