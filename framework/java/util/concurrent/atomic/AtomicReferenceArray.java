package java.util.concurrent.atomic;

import java.io.Serializable;

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
}
