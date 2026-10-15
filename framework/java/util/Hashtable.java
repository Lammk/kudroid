package java.util;

import java.io.Serializable;

public class Hashtable<K,V> extends Dictionary<K,V> implements Map<K,V>, Cloneable, Serializable {
    private static final long serialVersionUID = 1421746759512286392L;
    private final HashMap<K,V> map;

    public Hashtable(int initialCapacity, float loadFactor) { map = new HashMap<K,V>(initialCapacity, loadFactor); }
    public Hashtable(int initialCapacity) { map = new HashMap<K,V>(initialCapacity); }
    public Hashtable() { map = new HashMap<K,V>(); }
    public Hashtable(Map<? extends K, ? extends V> t) { map = new HashMap<K,V>(t); }

    public synchronized int size() { return map.size(); }
    public synchronized boolean isEmpty() { return map.isEmpty(); }
    public synchronized Enumeration<K> keys() { return Collections.enumeration(map.keySet()); }
    public synchronized Enumeration<V> elements() { return Collections.enumeration(map.values()); }
    public synchronized boolean contains(Object value) { return map.containsValue(value); }
    public boolean containsValue(Object value) { return contains(value); }
    public synchronized boolean containsKey(Object key) { return map.containsKey(key); }
    public synchronized V get(Object key) { return map.get(key); }
    public synchronized V put(K key, V value) {
        if (value == null) throw new NullPointerException();
        return map.put(key, value);
    }
    public synchronized V remove(Object key) { return map.remove(key); }
    public synchronized void putAll(Map<? extends K, ? extends V> t) { map.putAll(t); }
    public synchronized void clear() { map.clear(); }
    public Set<K> keySet() { return map.keySet(); }
    public Set<Map.Entry<K,V>> entrySet() { return map.entrySet(); }
    public Collection<V> values() { return map.values(); }
}
