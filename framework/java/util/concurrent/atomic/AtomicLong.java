package java.util.concurrent.atomic;

import java.util.function.LongUnaryOperator;
import java.util.function.LongBinaryOperator;

public class AtomicLong extends Number {

    private volatile long value;

    public AtomicLong() {
    }

    public AtomicLong(long initialValue) {
        value = initialValue;
    }

    public final long get() {
        return value;
    }

    public final synchronized void set(long newValue) {
        value = newValue;
    }

    public final synchronized void lazySet(long newValue) {
        value = newValue;
    }

    public final synchronized long getAndSet(long newValue) {
        long old = value;
        value = newValue;
        return old;
    }

    public final synchronized boolean compareAndSet(long expect, long update) {
        if (value != expect) {
            return false;
        }
        value = update;
        return true;
    }

    public final synchronized long getAndIncrement() {
        return value++;
    }

    public final synchronized long getAndDecrement() {
        return value--;
    }

    public final synchronized long getAndAdd(long delta) {
        long old = value;
        value += delta;
        return old;
    }

    public final synchronized long incrementAndGet() {
        return ++value;
    }

    public final synchronized long decrementAndGet() {
        return --value;
    }

    public final synchronized long addAndGet(long delta) {
        value += delta;
        return value;
    }

    public int intValue() {
        return (int) value;
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
        return Long.toString(value);
    }

    public final long getAndUpdate(LongUnaryOperator f) {
        long prev, next;
        do {
            prev = get();
            next = f.applyAsLong(prev);
        } while (!compareAndSet(prev, next));
        return prev;
    }

    public final long updateAndGet(LongUnaryOperator f) {
        long prev, next;
        do {
            prev = get();
            next = f.applyAsLong(prev);
        } while (!compareAndSet(prev, next));
        return next;
    }

    public final long getAndAccumulate(long x, LongBinaryOperator f) {
        long prev, next;
        do {
            prev = get();
            next = f.applyAsLong(prev, x);
        } while (!compareAndSet(prev, next));
        return prev;
    }

    public final long accumulateAndGet(long x, LongBinaryOperator f) {
        long prev, next;
        do {
            prev = get();
            next = f.applyAsLong(prev, x);
        } while (!compareAndSet(prev, next));
        return next;
    }

    public final long getPlain() { return get(); }
    public final void setPlain(long newValue) { set(newValue); }
    public final long getOpaque() { return get(); }
    public final void setOpaque(long newValue) { set(newValue); }
    public final long getAcquire() { return get(); }
    public final void setRelease(long newValue) { set(newValue); }
    public final synchronized long compareAndExchange(long expect, long update) {
        long witness = get();
        if (witness == expect) set(update);
        return witness;
    }
    public final boolean weakCompareAndSetPlain(long expect, long update) {
        return compareAndSet(expect, update);
    }
    public final boolean weakCompareAndSet(long expect, long update) {
        return compareAndSet(expect, update);
    }
}
