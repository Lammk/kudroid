package android.os;

/**
 * android.os.SystemClock — đồng hồ đơn điệu của hệ thống.
 */
public final class SystemClock {
    private SystemClock() {
    }

    /** Millisecond kể từ khi boot, không bị nhảy khi user đổi giờ hệ thống. */
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
        return false; // app không có quyền đổi giờ hệ thống
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
                // SystemClock.sleep không bao giờ ném; ngủ tiếp cho đủ thời gian.
            }
        }
    }
}
