package java.util.concurrent.atomic;

public class AtomicInteger extends Number {

    private volatile int value;

    public AtomicInteger() {
    }

    public AtomicInteger(int initialValue) {
        value = initialValue;
    }

    public final int get() {
        return value;
    }

    public final void set(int newValue) {
        value = newValue;
    }

    public final void lazySet(int newValue) {
        value = newValue;
    }

    public final synchronized int getAndSet(int newValue) {
        int old = value;
        value = newValue;
        return old;
    }

    public final synchronized boolean compareAndSet(int expect, int update) {
        if (value != expect) {
            return false;
        }
        value = update;
        return true;
    }

    public final boolean weakCompareAndSet(int expect, int update) {
        return compareAndSet(expect, update);
    }

    public final synchronized int getAndIncrement() {
        return value++;
    }

    public final synchronized int getAndDecrement() {
        return value--;
    }

    public final synchronized int getAndAdd(int delta) {
        int old = value;
        value += delta;
        return old;
    }

    public final synchronized int incrementAndGet() {
        return ++value;
    }

    public final synchronized int decrementAndGet() {
        return --value;
    }

    public final synchronized int addAndGet(int delta) {
        value += delta;
        return value;
    }

    public int intValue() {
        return value;
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
        return Integer.toString(value);
    }
}
