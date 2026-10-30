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

    public final synchronized void set(boolean newValue) {
        value = newValue;
    }

    public final synchronized void lazySet(boolean newValue) {
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

    public final boolean getPlain() { return get(); }
    public final void setPlain(boolean newValue) { set(newValue); }
    public final boolean getOpaque() { return get(); }
    public final void setOpaque(boolean newValue) { set(newValue); }
    public final boolean getAcquire() { return get(); }
    public final void setRelease(boolean newValue) { set(newValue); }
    public final synchronized boolean compareAndExchange(boolean expect, boolean update) {
        boolean witness = get();
        if (witness == expect) set(update);
        return witness;
    }
    public final boolean weakCompareAndSetPlain(boolean expect, boolean update) {
        return compareAndSet(expect, update);
    }
}
