package java.lang;

import java.util.HashMap;

public abstract class ClassValue<T> {
    private final HashMap<Class<?>, Entry<T>> values = new HashMap<>();

    private static final class Entry<V> {
        V value;
        boolean initialized;
    }

    protected ClassValue() {}

    protected abstract T computeValue(Class<?> type);

    public T get(Class<?> type) {
        if (type == null) throw new NullPointerException();
        for (;;) {
            Entry<T> entry;
            synchronized (values) {
                entry = values.get(type);
                if (entry == null) {
                    entry = new Entry<T>();
                    values.put(type, entry);
                }
                if (entry.initialized) return entry.value;
            }
            // User computation may reenter or block without holding the cache lock.
            T computed = computeValue(type);
            synchronized (values) {
                if (values.get(type) != entry) continue;
                if (!entry.initialized) {
                    entry.value = computed;
                    entry.initialized = true;
                }
                return entry.value;
            }
        }
    }

    public void remove(Class<?> type) {
        if (type == null) throw new NullPointerException();
        synchronized (values) {
            Entry<T> entry = values.get(type);
            if (entry != null && entry.initialized) values.remove(type);
        }
    }
}
