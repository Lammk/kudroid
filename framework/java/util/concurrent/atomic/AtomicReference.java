package java.util.concurrent.atomic;

public class AtomicReference<V> {

    private volatile V value;

    public AtomicReference() {
    }

    public AtomicReference(V initialValue) {
        value = initialValue;
    }

    public final V get() {
        return value;
    }

    public final void set(V newValue) {
        value = newValue;
    }

    public final void lazySet(V newValue) {
        value = newValue;
    }

    public final synchronized boolean compareAndSet(V expect, V update) {
        if (value != expect) {
            return false;
        }
        value = update;
        return true;
    }

    public final boolean weakCompareAndSet(V expect, V update) {
        return compareAndSet(expect, update);
    }

    public final synchronized V getAndSet(V newValue) {
        V old = value;
        value = newValue;
        return old;
    }

    public String toString() {
        return String.valueOf(value);
    }
}
