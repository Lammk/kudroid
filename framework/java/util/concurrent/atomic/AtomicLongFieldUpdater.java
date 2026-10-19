package java.util.concurrent.atomic;

import java.lang.reflect.Field;

public abstract class AtomicLongFieldUpdater<T> {
    protected AtomicLongFieldUpdater() {}

    public static <U> AtomicLongFieldUpdater<U> newUpdater(Class<U> owner, String name) {
        return new Updater<U>(owner, FieldUpdaterSupport.find(owner, long.class, name));
    }

    public abstract boolean compareAndSet(T object, long expect, long update);
    public abstract boolean weakCompareAndSet(T object, long expect, long update);
    public abstract void set(T object, long value);
    public abstract void lazySet(T object, long value);
    public abstract long get(T object);

    public long getAndSet(T object, long value) {
        long old;
        do { old = get(object); } while (!compareAndSet(object, old, value));
        return old;
    }

    public long getAndAdd(T object, long delta) {
        long old;
        do { old = get(object); } while (!compareAndSet(object, old, old + delta));
        return old;
    }

    public long getAndIncrement(T object) { return getAndAdd(object, 1); }
    public long getAndDecrement(T object) { return getAndAdd(object, -1); }
    public long addAndGet(T object, long delta) { return getAndAdd(object, delta) + delta; }
    public long incrementAndGet(T object) { return addAndGet(object, 1); }
    public long decrementAndGet(T object) { return addAndGet(object, -1); }

    private static final class Updater<U> extends AtomicLongFieldUpdater<U> {
        private final Class<U> owner;
        private final Field field;
        Updater(Class<U> owner, Field field) { this.owner = owner; this.field = field; }

        public long get(U object) {
            FieldUpdaterSupport.check(owner, object);
            synchronized (object) {
                try { return field.getLong(object); }
                catch (IllegalAccessException e) { throw new RuntimeException(e); }
            }
        }

        public void set(U object, long value) {
            FieldUpdaterSupport.check(owner, object);
            synchronized (object) {
                try { field.setLong(object, value); }
                catch (IllegalAccessException e) { throw new RuntimeException(e); }
            }
        }

        public void lazySet(U object, long value) { set(object, value); }
        public boolean compareAndSet(U object, long expect, long update) {
            FieldUpdaterSupport.check(owner, object);
            synchronized (object) {
                if (get(object) != expect) return false;
                set(object, update);
                return true;
            }
        }
        public boolean weakCompareAndSet(U object, long expect, long update) {
            return compareAndSet(object, expect, update);
        }
    }
}
