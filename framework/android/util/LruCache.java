package android.util;

import java.util.LinkedHashMap;
import java.util.Map;

public class LruCache<K, V> {
    private final LinkedHashMap<K, V> map;
    private int size;
    private int maxSize;

    public LruCache(int maxSize) {
        if (maxSize <= 0) throw new IllegalArgumentException("maxSize <= 0");
        this.maxSize = maxSize;
        this.map = new LinkedHashMap<K, V>(0, 0.75f, true);
    }
    public final synchronized V get(K key) {
        if (key == null) return null;
        return map.get(key);
    }
    public final synchronized V put(K key, V value) {
        if (key == null || value == null) return null;
        V previous = map.put(key, value);
        size += sizeOf(key, value);
        trimToSize(maxSize);
        return previous;
    }
    public void trimToSize(int maxSize) {
        while (size > maxSize && !map.isEmpty()) {
            Map.Entry<K, V> toEvict = map.entrySet().iterator().next();
            K key = toEvict.getKey();
            V value = toEvict.getValue();
            map.remove(key);
            size -= sizeOf(key, value);
            entryRemoved(true, key, value, null);
        }
    }
    public final synchronized V remove(K key) {
        if (key == null) return null;
        V previous = map.remove(key);
        if (previous != null) size -= sizeOf(key, previous);
        return previous;
    }
    protected void entryRemoved(boolean evicted, K key, V oldValue, V newValue) {}
    protected int sizeOf(K key, V value) { return 1; }
    public final synchronized int size() { return size; }
    public final synchronized int maxSize() { return maxSize; }
}
