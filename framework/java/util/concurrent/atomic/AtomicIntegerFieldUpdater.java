package java.util.concurrent.atomic;

import java.lang.reflect.Field;

public abstract class AtomicIntegerFieldUpdater<T> {
    protected AtomicIntegerFieldUpdater() {}

    public static <U> AtomicIntegerFieldUpdater<U> newUpdater(Class<U> owner, String name) {
        return new Updater<U>(owner, FieldUpdaterSupport.find(owner, int.class, name));
    }

    public abstract boolean compareAndSet(T object, int expect, int update);
    public abstract boolean weakCompareAndSet(T object, int expect, int update);
    public abstract void set(T object, int value);
    public abstract void lazySet(T object, int value);
    public abstract int get(T object);

    public int getAndSet(T object, int value) {
        int old;
        do { old = get(object); } while (!compareAndSet(object, old, value));
        return old;
    }

    public int getAndAdd(T object, int delta) {
        int old;
        do { old = get(object); } while (!compareAndSet(object, old, old + delta));
        return old;
    }

    public int getAndIncrement(T object) { return getAndAdd(object, 1); }
    public int getAndDecrement(T object) { return getAndAdd(object, -1); }
    public int addAndGet(T object, int delta) { return getAndAdd(object, delta) + delta; }
    public int incrementAndGet(T object) { return addAndGet(object, 1); }
    public int decrementAndGet(T object) { return addAndGet(object, -1); }

    private static final class Updater<U> extends AtomicIntegerFieldUpdater<U> {
        private final Class<U> owner;
        private final Field field;
        Updater(Class<U> owner, Field field) { this.owner = owner; this.field = field; }

        public int get(U object) {
            FieldUpdaterSupport.check(owner, object);
            synchronized (object) {
                try { return field.getInt(object); }
                catch (IllegalAccessException e) { throw new RuntimeException(e); }
            }
        }

        public void set(U object, int value) {
            FieldUpdaterSupport.check(owner, object);
            synchronized (object) {
                try { field.setInt(object, value); }
                catch (IllegalAccessException e) { throw new RuntimeException(e); }
            }
        }

        public void lazySet(U object, int value) { set(object, value); }
        public boolean compareAndSet(U object, int expect, int update) {
            FieldUpdaterSupport.check(owner, object);
            synchronized (object) {
                if (get(object) != expect) return false;
                set(object, update);
                return true;
            }
        }
        public boolean weakCompareAndSet(U object, int expect, int update) {
            return compareAndSet(object, expect, update);
        }
    }
}
