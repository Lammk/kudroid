package android.os;

/**
 * android.os.SystemClock — system monotonous clock.
 */
public final class SystemClock {
    private SystemClock() {
    }

    /** Milliseconds since boot, do not jump when the user changes the system time. */
    public static long uptimeMillis() {
        return System.nanoTime() / 1000000L;
    }

    public static long elapsedRealtime() {
        return uptimeMillis();
    }

    public static long elapsedRealtimeNanos() {
        return System.nanoTime();
    }

    public static long currentThreadTimeMillis() {
        return uptimeMillis();
    }

    public static boolean setCurrentTimeMillis(long millis) {
        return false; // The app does not have the right to change the system time
    }

    public static void sleep(long ms) {
        if (ms <= 0) return;
        long deadline = uptimeMillis() + ms;
        for (;;) {
            long remaining = deadline - uptimeMillis();
            if (remaining <= 0) return;
            try {
                Thread.sleep(remaining);
            } catch (InterruptedException ignored) {
                // SystemClock.sleep never throws; Continue sleeping for enough time.
            }
        }
    }
}
