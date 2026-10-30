package java.util.concurrent.atomic;

import java.io.Serializable;
import java.util.function.LongUnaryOperator;
import java.util.function.LongBinaryOperator;

public class AtomicLongArray implements Serializable {
    private static final long serialVersionUID = -2308431214976778248L;
    private final long[] array;

    public AtomicLongArray(int length) {
        this.array = new long[length];
    }
    public AtomicLongArray(long[] array) {
        this.array = array.clone();
    }
    public final int length() { return array.length; }
    public final synchronized long get(int i) { return array[i]; }
    public final synchronized void set(int i, long newValue) { array[i] = newValue; }
    public final synchronized long getAndSet(int i, long newValue) {
        long old = array[i];
        array[i] = newValue;
        return old;
    }
    public final synchronized boolean compareAndSet(int i, long expect, long update) {
        if (array[i] == expect) {
            array[i] = update;
            return true;
        }
        return false;
    }
    public final synchronized long getAndIncrement(int i) { return array[i]++; }
    public final synchronized long getAndDecrement(int i) { return array[i]--; }
    public final synchronized long getAndAdd(int i, long delta) {
        long old = array[i];
        array[i] += delta;
        return old;
    }
    public final synchronized long incrementAndGet(int i) { return ++array[i]; }
    public final synchronized long decrementAndGet(int i) { return --array[i]; }
    public final synchronized long addAndGet(int i, long delta) { return array[i] += delta; }
    public final void lazySet(int i, long newValue) { set(i, newValue); }
    public final boolean weakCompareAndSet(int i, long expect, long update) {
        return compareAndSet(i, expect, update);
    }
    public final boolean weakCompareAndSetPlain(int i, long expect, long update) {
        return compareAndSet(i, expect, update);
    }
    public final long getPlain(int i) { return get(i); }
    public final void setPlain(int i, long newValue) { set(i, newValue); }
    public final long getOpaque(int i) { return get(i); }
    public final void setOpaque(int i, long newValue) { set(i, newValue); }
    public final long getAcquire(int i) { return get(i); }
    public final void setRelease(int i, long newValue) { set(i, newValue); }
    public final synchronized long compareAndExchange(int i, long expect, long update) {
        long witness = array[i];
        if (witness == expect) array[i] = update;
        return witness;
    }
    public final synchronized long getAndUpdate(int i, LongUnaryOperator f) {
        long prev = array[i];
        array[i] = f.applyAsLong(prev);
        return prev;
    }
    public final synchronized long updateAndGet(int i, LongUnaryOperator f) {
        return array[i] = f.applyAsLong(array[i]);
    }
    public final synchronized long getAndAccumulate(int i, long x, LongBinaryOperator f) {
        long prev = array[i];
        array[i] = f.applyAsLong(prev, x);
        return prev;
    }
    public final synchronized long accumulateAndGet(int i, long x, LongBinaryOperator f) {
        return array[i] = f.applyAsLong(array[i], x);
    }
    public synchronized String toString() { return java.util.Arrays.toString(array); }
}
