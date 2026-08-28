package java.util;

import java.io.Serializable;

public class TreeMap<K,V> extends AbstractMap<K,V> implements NavigableMap<K,V>, Cloneable, Serializable {
    private static final long serialVersionUID = 919286545866124006L;
    private final Comparator<? super K> comparator;
    private final List<K> keys = new ArrayList<K>();
    private final List<V> values = new ArrayList<V>();

    public TreeMap() { this.comparator = null; }
    public TreeMap(Comparator<? super K> comparator) { this.comparator = comparator; }
    public TreeMap(Map<? extends K, ? extends V> m) {
        this();
        if (m != null) putAll(m);
    }
    public TreeMap(SortedMap<K, ? extends V> m) {
        this(m != null ? m.comparator() : null);
        if (m != null) putAll(m);
    }

    public int size() { return keys.size(); }
    public boolean isEmpty() { return keys.isEmpty(); }
    public boolean containsKey(Object key) { return keys.contains(key); }
    public boolean containsValue(Object value) { return values.contains(value); }
    public V get(Object key) {
        int idx = keys.indexOf(key);
        return idx >= 0 ? values.get(idx) : null;
    }
    public Comparator<? super K> comparator() { return comparator; }
    public K firstKey() { if (keys.isEmpty()) throw new NoSuchElementException(); return keys.get(0); }
    public K lastKey() { if (keys.isEmpty()) throw new NoSuchElementException(); return keys.get(keys.size() - 1); }

    public V put(K key, V value) {
        if (key == null && comparator == null) throw new NullPointerException();
        int idx = keys.indexOf(key);
        if (idx >= 0) {
            return values.set(idx, value);
        } else {
            keys.add(key);
            values.add(value);
            return null;
        }
    }

    public void putAll(Map<? extends K, ? extends V> map) {
        if (map == null) throw new NullPointerException();
        for (Map.Entry<? extends K, ? extends V> e : map.entrySet()) {
            put(e.getKey(), e.getValue());
        }
    }

    public V remove(Object key) {
        int idx = keys.indexOf(key);
        if (idx >= 0) {
            keys.remove(idx);
            return values.remove(idx);
        }
        return null;
    }

    public void clear() {
        keys.clear();
        values.clear();
    }

    public Set<K> keySet() {
        return new LinkedHashSet<K>(keys);
    }

    public Collection<V> values() {
        return new ArrayList<V>(values);
    }

    public Set<Map.Entry<K, V>> entrySet() {
        Set<Map.Entry<K, V>> set = new LinkedHashSet<Map.Entry<K, V>>();
        for (int i = 0; i < keys.size(); i++) {
            final K k = keys.get(i);
            final V v = values.get(i);
            set.add(new Map.Entry<K, V>() {
                public K getKey() { return k; }
                public V getValue() { return v; }
                public V setValue(V value) { return null; }
            });
        }
        return set;
    }

    public Map.Entry<K,V> lowerEntry(K key) { return null; }
    public K lowerKey(K key) { return null; }
    public Map.Entry<K,V> floorEntry(K key) { return null; }
    public K floorKey(K key) { return null; }
    public Map.Entry<K,V> ceilingEntry(K key) { return null; }
    public K ceilingKey(K key) { return null; }
    public Map.Entry<K,V> higherEntry(K key) { return null; }
    public K higherKey(K key) { return null; }
    public Map.Entry<K,V> firstEntry() { return null; }
    public Map.Entry<K,V> lastEntry() { return null; }
    public Map.Entry<K,V> pollFirstEntry() { return null; }
    public Map.Entry<K,V> pollLastEntry() { return null; }
    public NavigableMap<K,V> descendingMap() { return this; }
    public NavigableSet<K> navigableKeySet() { return new TreeSet<K>(keys); }
    public NavigableSet<K> descendingKeySet() { return new TreeSet<K>(keys); }
    public NavigableMap<K,V> subMap(K fromKey, boolean fromInclusive, K toKey, boolean toInclusive) { return this; }
    public NavigableMap<K,V> headMap(K toKey, boolean inclusive) { return this; }
    public NavigableMap<K,V> tailMap(K fromKey, boolean inclusive) { return this; }
    public SortedMap<K,V> subMap(K fromKey, K toKey) { return this; }
    public SortedMap<K,V> headMap(K toKey) { return this; }
    public SortedMap<K,V> tailMap(K fromKey) { return this; }
}
