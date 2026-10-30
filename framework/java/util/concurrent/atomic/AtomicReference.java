package java.util.concurrent.atomic;

import java.util.function.BinaryOperator;
import java.util.function.UnaryOperator;

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

    public final synchronized void set(V newValue) {
        value = newValue;
    }

    public final synchronized void lazySet(V newValue) {
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

    public final V getAndUpdate(UnaryOperator<V> f) {
        V prev, next;
        do {
            prev = get();
            next = f.apply(prev);
        } while (!compareAndSet(prev, next));
        return prev;
    }

    public final V updateAndGet(UnaryOperator<V> f) {
        V prev, next;
        do {
            prev = get();
            next = f.apply(prev);
        } while (!compareAndSet(prev, next));
        return next;
    }

    public final V getAndAccumulate(V x, BinaryOperator<V> f) {
        V prev, next;
        do {
            prev = get();
            next = f.apply(prev, x);
        } while (!compareAndSet(prev, next));
        return prev;
    }

    public final V accumulateAndGet(V x, BinaryOperator<V> f) {
        V prev, next;
        do {
            prev = get();
            next = f.apply(prev, x);
        } while (!compareAndSet(prev, next));
        return next;
    }

    public final V getPlain() { return get(); }
    public final void setPlain(V newValue) { set(newValue); }
    public final V getOpaque() { return get(); }
    public final void setOpaque(V newValue) { set(newValue); }
    public final V getAcquire() { return get(); }
    public final void setRelease(V newValue) { set(newValue); }
    public final synchronized V compareAndExchange(V expect, V update) {
        V witness = get();
        if (witness == expect) set(update);
        return witness;
    }
    public final boolean weakCompareAndSetPlain(V expect, V update) {
        return compareAndSet(expect, update);
    }
}
