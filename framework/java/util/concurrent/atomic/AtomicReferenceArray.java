package java.util.concurrent.atomic;

import java.io.Serializable;
import java.util.function.BinaryOperator;
import java.util.function.UnaryOperator;

public class AtomicReferenceArray<E> implements Serializable {
    private static final long serialVersionUID = -6209656149925076988L;
    private final Object[] array;

    public AtomicReferenceArray(int length) {
        this.array = new Object[length];
    }
    public AtomicReferenceArray(E[] array) {
        this.array = array.clone();
    }
    public final int length() { return array.length; }
    @SuppressWarnings("unchecked")
    public final synchronized E get(int i) { return (E) array[i]; }
    public final synchronized void set(int i, E newValue) { array[i] = newValue; }
    public final void lazySet(int i, E newValue) { set(i, newValue); }
    @SuppressWarnings("unchecked")
    public final synchronized E getAndSet(int i, E newValue) {
        E old = (E) array[i];
        array[i] = newValue;
        return old;
    }
    public final synchronized boolean compareAndSet(int i, E expect, E update) {
        if (array[i] == expect) {
            array[i] = update;
            return true;
        }
        return false;
    }
    public final boolean weakCompareAndSet(int i, E expect, E update) {
        return compareAndSet(i, expect, update);
    }
    public final boolean weakCompareAndSetPlain(int i, E expect, E update) {
        return compareAndSet(i, expect, update);
    }
    public final E getPlain(int i) { return get(i); }
    public final void setPlain(int i, E newValue) { set(i, newValue); }
    public final E getOpaque(int i) { return get(i); }
    public final void setOpaque(int i, E newValue) { set(i, newValue); }
    public final E getAcquire(int i) { return get(i); }
    public final void setRelease(int i, E newValue) { set(i, newValue); }
    @SuppressWarnings("unchecked")
    public final synchronized E compareAndExchange(int i, E expect, E update) {
        E witness = (E) array[i];
        if (witness == expect) array[i] = update;
        return witness;
    }
    @SuppressWarnings("unchecked")
    public final synchronized E getAndUpdate(int i, UnaryOperator<E> f) {
        E prev = (E) array[i];
        array[i] = f.apply(prev);
        return prev;
    }
    @SuppressWarnings("unchecked")
    public final synchronized E updateAndGet(int i, UnaryOperator<E> f) {
        E next = f.apply((E) array[i]);
        array[i] = next;
        return next;
    }
    @SuppressWarnings("unchecked")
    public final synchronized E getAndAccumulate(int i, E x, BinaryOperator<E> f) {
        E prev = (E) array[i];
        array[i] = f.apply(prev, x);
        return prev;
    }
    @SuppressWarnings("unchecked")
    public final synchronized E accumulateAndGet(int i, E x, BinaryOperator<E> f) {
        E next = f.apply((E) array[i], x);
        array[i] = next;
        return next;
    }
    public synchronized String toString() {
        return java.util.Arrays.toString(array);
    }
}
