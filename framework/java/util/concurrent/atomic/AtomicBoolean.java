package java.util.concurrent.atomic;

public class AtomicBoolean {

    private volatile boolean value;

    public AtomicBoolean() {
    }

    public AtomicBoolean(boolean initialValue) {
        value = initialValue;
    }

    public final boolean get() {
        return value;
    }

    public final void set(boolean newValue) {
        value = newValue;
    }

    public final void lazySet(boolean newValue) {
        value = newValue;
    }

    public final synchronized boolean compareAndSet(boolean expect, boolean update) {
        if (value != expect) {
            return false;
        }
        value = update;
        return true;
    }

    public final boolean weakCompareAndSet(boolean expect, boolean update) {
        return compareAndSet(expect, update);
    }

    public final synchronized boolean getAndSet(boolean newValue) {
        boolean old = value;
        value = newValue;
        return old;
    }

    public String toString() {
        return Boolean.toString(value);
    }
}
