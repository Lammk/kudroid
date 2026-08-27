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

    public long toNanos(long duration) {
        return duration * nanosPerUnit;
    }

    public long toMicros(long duration) {
        return toNanos(duration) / 1000L;
    }

    public long toMillis(long duration) {
        return toNanos(duration) / 1000000L;
    }

    public long toSeconds(long duration) {
        return toNanos(duration) / 1000000000L;
    }

    public long toMinutes(long duration) {
        return toSeconds(duration) / 60L;
    }

    public long toHours(long duration) {
        return toMinutes(duration) / 60L;
    }

    public long toDays(long duration) {
        return toHours(duration) / 24L;
    }

    public long convert(long duration, TimeUnit sourceUnit) {
        return sourceUnit.toNanos(duration) / nanosPerUnit;
    }

    public void sleep(long timeout) throws InterruptedException {
        long nanos = toNanos(timeout);
        Thread.sleep(nanos / 1000000L, (int) (nanos % 1000000L));
    }
}
