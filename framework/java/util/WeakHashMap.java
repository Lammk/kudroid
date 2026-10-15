package java.util;

public class WeakHashMap<K,V> extends HashMap<K,V> {
    public WeakHashMap(int initialCapacity, float loadFactor) { super(initialCapacity, loadFactor); }
    public WeakHashMap(int initialCapacity) { super(initialCapacity); }
    public WeakHashMap() { super(); }
    public WeakHashMap(Map<? extends K, ? extends V> m) { super(m); }
}
