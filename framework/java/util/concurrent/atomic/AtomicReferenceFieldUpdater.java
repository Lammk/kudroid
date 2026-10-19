package java.util.concurrent.atomic;

import java.lang.reflect.Field;

public abstract class AtomicReferenceFieldUpdater<T, V> {
    protected AtomicReferenceFieldUpdater() {}

    public static <U, W> AtomicReferenceFieldUpdater<U, W> newUpdater(
            Class<U> owner, Class<W> type, String name) {
        if (type == null) throw new NullPointerException();
        if (type.isPrimitive()) throw new ClassCastException("Reference type required");
        return new Updater<U, W>(owner, type, FieldUpdaterSupport.find(owner, type, name));
    }

    public abstract boolean compareAndSet(T object, V expect, V update);
    public abstract boolean weakCompareAndSet(T object, V expect, V update);
    public abstract void set(T object, V value);
    public abstract void lazySet(T object, V value);
    public abstract V get(T object);

    public V getAndSet(T object, V value) {
        V old;
        do { old = get(object); } while (!compareAndSet(object, old, value));
        return old;
    }

    private static final class Updater<U, W> extends AtomicReferenceFieldUpdater<U, W> {
        private final Class<U> owner;
        private final Class<W> type;
        private final Field field;
        Updater(Class<U> owner, Class<W> type, Field field) {
            this.owner = owner; this.type = type; this.field = field;
        }
        @SuppressWarnings("unchecked")
        public W get(U object) {
            FieldUpdaterSupport.check(owner, object);
            synchronized (object) {
                try { return (W) field.get(object); }
                catch (IllegalAccessException e) { throw new RuntimeException(e); }
            }
        }
        private void checkValue(W value) {
            if (value != null && !type.isInstance(value)) throw new ClassCastException();
        }
        public void set(U object, W value) {
            FieldUpdaterSupport.check(owner, object);
            checkValue(value);
            synchronized (object) {
                try { field.set(object, value); }
                catch (IllegalAccessException e) { throw new RuntimeException(e); }
            }
        }
        public void lazySet(U object, W value) { set(object, value); }
        public boolean compareAndSet(U object, W expect, W update) {
            FieldUpdaterSupport.check(owner, object);
            checkValue(update);
            synchronized (object) {
                if (get(object) != expect) return false;
                set(object, update);
                return true;
            }
        }
        public boolean weakCompareAndSet(U object, W expect, W update) {
            return compareAndSet(object, expect, update);
        }
    }
}
