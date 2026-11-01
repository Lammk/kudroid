package java.util.concurrent;

public class ConcurrentHashMap<K, V> extends java.util.HashMap<K, V> {

    public ConcurrentHashMap() {
    }

    public ConcurrentHashMap(int initialCapacity) {
        super(initialCapacity);
    }

    public ConcurrentHashMap(int initialCapacity, float loadFactor, int concurrencyLevel) {
        super(checkedCapacity(initialCapacity, loadFactor, concurrencyLevel), loadFactor);
    }

    private static int checkedCapacity(int initialCapacity, float loadFactor, int concurrencyLevel) {
        if (initialCapacity < 0 || !(loadFactor > 0.0f) || concurrencyLevel <= 0) {
            throw new IllegalArgumentException();
        }
        // Prevent overflow while the backing table rounds up to a power of two.
        if (initialCapacity > (1 << 30)) {
            throw new OutOfMemoryError("Required array size too large");
        }
        return initialCapacity;
    }

    public ConcurrentHashMap(java.util.Map<? extends K, ? extends V> m) {
        super(m);
    }

    public synchronized V get(Object key) {
        return super.get(key);
    }

    public synchronized V put(K key, V value) {
        return super.put(key, value);
    }

    public synchronized V putIfAbsent(K key, V value) {
        return super.putIfAbsent(key, value);
    }

    public synchronized V remove(Object key) {
        return super.remove(key);
    }

    public synchronized boolean containsKey(Object key) {
        return super.containsKey(key);
    }

    public synchronized int size() {
        return super.size();
    }

    public synchronized void clear() {
        super.clear();
    }
}
