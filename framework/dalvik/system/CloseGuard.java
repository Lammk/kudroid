package dalvik.system;

/** Leak detection helper; the guard is accepted and never reports. */
public final class CloseGuard {
    public interface Reporter {
        void report(String message, Throwable allocationSite);
    }

    private static final CloseGuard NOOP = new CloseGuard();
    private static volatile boolean stackAndTrackingEnabled = true;
    private static volatile Reporter reporter;

    private CloseGuard() {}

    public static CloseGuard get() {
        return new CloseGuard();
    }

    public static void setEnabled(boolean enabled) {
        stackAndTrackingEnabled = enabled;
    }

    public static boolean isEnabled() {
        return stackAndTrackingEnabled;
    }

    public static void setReporter(Reporter r) {
        if (r == null) {
            throw new NullPointerException("reporter == null");
        }
        reporter = r;
    }

    public static Reporter getReporter() {
        return reporter;
    }

    public void open(String closer) {
        if (closer == null) {
            throw new NullPointerException("closer == null");
        }
    }

    public void close() {
    }

    public void warnIfOpen() {
    }
}
