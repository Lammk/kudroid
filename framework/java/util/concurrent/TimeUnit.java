package java.util.concurrent;

public enum TimeUnit {

    NANOSECONDS(1L),
    MICROSECONDS(1000L),
    MILLISECONDS(1000000L),
    SECONDS(1000000000L),
    MINUTES(60L * 1000000000L),
    HOURS(3600L * 1000000000L),
    DAYS(86400L * 1000000000L);

    private final long nanosPerUnit;

    private TimeUnit(long nanosPerUnit) {
        this.nanosPerUnit = nanosPerUnit;
    }

    private long scale(long duration, long target) {
        if (nanosPerUnit <= target) return duration / (target / nanosPerUnit);
        long factor = nanosPerUnit / target;
        long limit = Long.MAX_VALUE / factor;
        if (duration > limit) return Long.MAX_VALUE;
        if (duration < -limit) return Long.MIN_VALUE;
        return duration * factor;
    }

    public long toNanos(long duration) { return scale(duration, 1L); }
    public long toMicros(long duration) { return scale(duration, 1000L); }
    public long toMillis(long duration) { return scale(duration, 1000000L); }
    public long toSeconds(long duration) { return scale(duration, 1000000000L); }
    public long toMinutes(long duration) { return scale(duration, 60000000000L); }
    public long toHours(long duration) { return scale(duration, 3600000000000L); }
    public long toDays(long duration) { return scale(duration, 86400000000000L); }
    public long convert(long duration, TimeUnit sourceUnit) {
        return sourceUnit.scale(duration, nanosPerUnit);
    }
    public void sleep(long timeout) throws InterruptedException {
        if (timeout > 0) {
            long millis = toMillis(timeout);
            int nanos = nanosPerUnit < 1000000L ?
                    (int) ((timeout % (1000000L / nanosPerUnit)) * nanosPerUnit) : 0;
            Thread.sleep(millis, nanos);
        }
    }
}
