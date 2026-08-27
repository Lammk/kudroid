package java.util.concurrent.atomic;

public class AtomicLong extends Number {

    private volatile long value;

    public AtomicLong() {
    }

    public AtomicLong(long initialValue) {
        value = initialValue;
    }

    public final long get() {
        return value;
    }

    public final void set(long newValue) {
        value = newValue;
    }

    public final void lazySet(long newValue) {
        value = newValue;
    }

    public final synchronized long getAndSet(long newValue) {
        long old = value;
        value = newValue;
        return old;
    }

    public final synchronized boolean compareAndSet(long expect, long update) {
        if (value != expect) {
            return false;
        }
        value = update;
        return true;
    }

    public final synchronized long getAndIncrement() {
        return value++;
    }

    public final synchronized long getAndDecrement() {
        return value--;
    }

    public final synchronized long getAndAdd(long delta) {
        long old = value;
        value += delta;
        return old;
    }

    public final synchronized long incrementAndGet() {
        return ++value;
    }

    public final synchronized long decrementAndGet() {
        return --value;
    }

    public final synchronized long addAndGet(long delta) {
        value += delta;
        return value;
    }

    public int intValue() {
        return (int) value;
    }

    public long longValue() {
        return value;
    }

    public float floatValue() {
        return value;
    }

    public double doubleValue() {
        return value;
    }

    public String toString() {
        return Long.toString(value);
    }
}
