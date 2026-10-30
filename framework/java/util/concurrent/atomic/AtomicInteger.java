package java.util.concurrent.atomic;

import java.util.function.IntUnaryOperator;
import java.util.function.IntBinaryOperator;

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

    public final synchronized void set(int newValue) {
        value = newValue;
    }

    public final synchronized void lazySet(int newValue) {
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

    public final int getAndUpdate(IntUnaryOperator f) {
        int prev, next;
        do {
            prev = get();
            next = f.applyAsInt(prev);
        } while (!compareAndSet(prev, next));
        return prev;
    }

    public final int updateAndGet(IntUnaryOperator f) {
        int prev, next;
        do {
            prev = get();
            next = f.applyAsInt(prev);
        } while (!compareAndSet(prev, next));
        return next;
    }

    public final int getAndAccumulate(int x, IntBinaryOperator f) {
        int prev, next;
        do {
            prev = get();
            next = f.applyAsInt(prev, x);
        } while (!compareAndSet(prev, next));
        return prev;
    }

    public final int accumulateAndGet(int x, IntBinaryOperator f) {
        int prev, next;
        do {
            prev = get();
            next = f.applyAsInt(prev, x);
        } while (!compareAndSet(prev, next));
        return next;
    }

    public final int getPlain() { return get(); }
    public final void setPlain(int newValue) { set(newValue); }
    public final int getOpaque() { return get(); }
    public final void setOpaque(int newValue) { set(newValue); }
    public final int getAcquire() { return get(); }
    public final void setRelease(int newValue) { set(newValue); }
    public final synchronized int compareAndExchange(int expect, int update) {
        int witness = get();
        if (witness == expect) set(update);
        return witness;
    }
    public final boolean weakCompareAndSetPlain(int expect, int update) {
        return compareAndSet(expect, update);
    }
}
