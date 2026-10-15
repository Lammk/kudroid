package java.util.concurrent.atomic;

import java.io.Serializable;

public class AtomicIntegerArray implements Serializable {
    private static final long serialVersionUID = 2862133569453604235L;
    private final int[] array;

    public AtomicIntegerArray(int length) {
        this.array = new int[length];
    }
    public AtomicIntegerArray(int[] array) {
        this.array = array.clone();
    }
    public final int length() { return array.length; }
    public final synchronized int get(int i) { return array[i]; }
    public final synchronized void set(int i, int newValue) { array[i] = newValue; }
    public final synchronized int getAndSet(int i, int newValue) {
        int old = array[i];
        array[i] = newValue;
        return old;
    }
    public final synchronized boolean compareAndSet(int i, int expect, int update) {
        if (array[i] == expect) {
            array[i] = update;
            return true;
        }
        return false;
    }
    public final synchronized int getAndIncrement(int i) { return array[i]++; }
    public final synchronized int getAndDecrement(int i) { return array[i]--; }
    public final synchronized int getAndAdd(int i, int delta) {
        int old = array[i];
        array[i] += delta;
        return old;
    }
    public final synchronized int incrementAndGet(int i) { return ++array[i]; }
    public final synchronized int decrementAndGet(int i) { return --array[i]; }
    public final synchronized int addAndGet(int i, int delta) { return array[i] += delta; }
}
