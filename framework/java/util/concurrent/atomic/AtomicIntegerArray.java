package java.util.concurrent.atomic;

import java.io.Serializable;
import java.util.function.IntUnaryOperator;
import java.util.function.IntBinaryOperator;

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
    public final void lazySet(int i, int newValue) { set(i, newValue); }
    public final boolean weakCompareAndSet(int i, int expect, int update) {
        return compareAndSet(i, expect, update);
    }
    public final boolean weakCompareAndSetPlain(int i, int expect, int update) {
        return compareAndSet(i, expect, update);
    }
    public final int getPlain(int i) { return get(i); }
    public final void setPlain(int i, int newValue) { set(i, newValue); }
    public final int getOpaque(int i) { return get(i); }
    public final void setOpaque(int i, int newValue) { set(i, newValue); }
    public final int getAcquire(int i) { return get(i); }
    public final void setRelease(int i, int newValue) { set(i, newValue); }
    public final synchronized int compareAndExchange(int i, int expect, int update) {
        int witness = array[i];
        if (witness == expect) array[i] = update;
        return witness;
    }
    public final synchronized int getAndUpdate(int i, IntUnaryOperator f) {
        int prev = array[i];
        array[i] = f.applyAsInt(prev);
        return prev;
    }
    public final synchronized int updateAndGet(int i, IntUnaryOperator f) {
        return array[i] = f.applyAsInt(array[i]);
    }
    public final synchronized int getAndAccumulate(int i, int x, IntBinaryOperator f) {
        int prev = array[i];
        array[i] = f.applyAsInt(prev, x);
        return prev;
    }
    public final synchronized int accumulateAndGet(int i, int x, IntBinaryOperator f) {
        return array[i] = f.applyAsInt(array[i], x);
    }
    public synchronized String toString() { return java.util.Arrays.toString(array); }
}
