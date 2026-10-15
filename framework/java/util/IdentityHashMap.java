package java.util;

public class IdentityHashMap<K,V> extends HashMap<K,V> {
    public IdentityHashMap() { super(); }
    public IdentityHashMap(int expectedMaxSize) { super(expectedMaxSize); }
    public IdentityHashMap(Map<? extends K, ? extends V> m) { super(m); }
}
