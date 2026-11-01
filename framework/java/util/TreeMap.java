package java.util;

import java.io.Serializable;

public class TreeMap<K,V> extends AbstractMap<K,V> implements NavigableMap<K,V>, Cloneable, Serializable {
    private static final long serialVersionUID = 919286545866124006L;

    private static final class Node<K,V> implements Map.Entry<K,V>, Serializable {
        final K key;
        V value;
        Node(K key, V value) { this.key = key; this.value = value; }
        public K getKey() { return key; }
        public V getValue() { return value; }
        public V setValue(V value) { V old = this.value; this.value = value; return old; }
        public boolean equals(Object o) {
            if (!(o instanceof Map.Entry)) return false;
            Map.Entry<?,?> e = (Map.Entry<?,?>) o;
            return equal(key, e.getKey()) && equal(value, e.getValue());
        }
        public int hashCode() { return (key == null ? 0 : key.hashCode()) ^ (value == null ? 0 : value.hashCode()); }
        public String toString() { return key + "=" + value; }
    }
    private static final class Store<K,V> implements Serializable {
        final List<Node<K,V>> nodes = new ArrayList<Node<K,V>>();
        final Comparator<? super K> comparator;
        int modifications;
        Store(Comparator<? super K> comparator) { this.comparator = comparator; }
    }

    private final Store<K,V> store;
    private final boolean descending;
    private final boolean hasLower, hasUpper, lowerInclusive, upperInclusive;
    private final K lower, upper;

    public TreeMap() { this((Comparator<? super K>) null); }
    public TreeMap(Comparator<? super K> comparator) {
        this(new Store<K,V>(comparator), false, false, null, false, false, null, false);
    }
    public TreeMap(Map<? extends K, ? extends V> m) { this(); putAll(m); }
    public TreeMap(SortedMap<K, ? extends V> m) { this(m.comparator()); putAll(m); }
    private TreeMap(Store<K,V> store, boolean descending, boolean hasLower, K lower, boolean lowerInclusive,
                    boolean hasUpper, K upper, boolean upperInclusive) {
        this.store = store; this.descending = descending;
        this.hasLower = hasLower; this.lower = lower; this.lowerInclusive = lowerInclusive;
        this.hasUpper = hasUpper; this.upper = upper; this.upperInclusive = upperInclusive;
    }
    private static boolean equal(Object a, Object b) { return a == null ? b == null : a.equals(b); }
    @SuppressWarnings("unchecked")
    private int compare(K a, K b) {
        return store.comparator == null ? ((Comparable<? super K>) a).compareTo(b) : store.comparator.compare(a, b);
    }
    private int viewCompare(K a, K b) { return descending ? compare(b, a) : compare(a, b); }
    private int insertionPoint(K key) {
        compare(key, key);
        int low = 0, high = store.nodes.size();
        while (low < high) {
            int mid = low + (high - low) / 2;
            if (compare(store.nodes.get(mid).key, key) < 0) low = mid + 1;
            else high = mid;
        }
        return low;
    }
    private boolean matches(int index, K key) {
        return index < store.nodes.size() && compare(store.nodes.get(index).key, key) == 0;
    }
    private boolean inRange(K key) {
        if (hasLower) {
            int c = compare(key, lower);
            if (c < 0 || (c == 0 && !lowerInclusive)) return false;
        }
        if (hasUpper) {
            int c = compare(key, upper);
            if (c > 0 || (c == 0 && !upperInclusive)) return false;
        }
        return true;
    }
    private int start() {
        if (!hasLower) return 0;
        int i = insertionPoint(lower);
        return i + (!lowerInclusive && matches(i, lower) ? 1 : 0);
    }
    private int end() {
        if (!hasUpper) return store.nodes.size();
        int i = insertionPoint(upper);
        return i + (upperInclusive && matches(i, upper) ? 1 : 0);
    }
    @SuppressWarnings("unchecked")
    private int index(Object key) {
        K k = (K) key;
        int i = insertionPoint(k);
        return inRange(k) && matches(i, k) ? i : -1;
    }
    public int size() { return Math.max(0, end() - start()); }
    public boolean isEmpty() { return size() == 0; }
    public boolean containsKey(Object key) { return index(key) >= 0; }
    public V get(Object key) { int i = index(key); return i < 0 ? null : store.nodes.get(i).value; }
    public Comparator<? super K> comparator() {
        if (!descending) return store.comparator;
        return new Comparator<K>() { public int compare(K a, K b) { return TreeMap.this.compare(b, a); } };
    }
    public V put(K key, V value) {
        int i = insertionPoint(key);
        if (!inRange(key)) throw new IllegalArgumentException("Key outside map range");
        if (matches(i, key)) return store.nodes.get(i).setValue(value);
        store.nodes.add(i, new Node<K,V>(key, value));
        store.modifications++;
        return null;
    }
    private V removeAt(int i) { V value = store.nodes.remove(i).value; store.modifications++; return value; }
    public V remove(Object key) { int i = index(key); return i < 0 ? null : removeAt(i); }
    public void clear() {
        int first = start(), last = end();
        for (int i = last - 1; i >= first; --i) removeAt(i);
    }
    private Map.Entry<K,V> snapshot(int i) {
        if (i < start() || i >= end()) return null;
        return new SimpleImmutableEntry<K,V>(store.nodes.get(i));
    }
    private int firstIndex() { return descending ? end() - 1 : start(); }
    private int lastIndex() { return descending ? start() : end() - 1; }
    public Map.Entry<K,V> firstEntry() { return isEmpty() ? null : snapshot(firstIndex()); }
    public Map.Entry<K,V> lastEntry() { return isEmpty() ? null : snapshot(lastIndex()); }
    private K keyOf(Map.Entry<K,V> entry) { return entry == null ? null : entry.getKey(); }
    public K firstKey() { if (isEmpty()) throw new NoSuchElementException(); return store.nodes.get(firstIndex()).key; }
    public K lastKey() { if (isEmpty()) throw new NoSuchElementException(); return store.nodes.get(lastIndex()).key; }
    private Map.Entry<K,V> poll(boolean first) {
        if (isEmpty()) return null;
        int i = first ? firstIndex() : lastIndex();
        Map.Entry<K,V> entry = snapshot(i);
        removeAt(i);
        return entry;
    }
    public Map.Entry<K,V> pollFirstEntry() { return poll(true); }
    public Map.Entry<K,V> pollLastEntry() { return poll(false); }
    private Map.Entry<K,V> neighbor(K key, boolean before, boolean inclusive) {
        int i = insertionPoint(key);
        boolean equal = matches(i, key);
        boolean ascendingBefore = descending ? !before : before;
        if (ascendingBefore) {
            i = equal && inclusive ? i : i - 1;
            i = Math.min(i, end() - 1);
        } else {
            i += equal && !inclusive ? 1 : 0;
            i = Math.max(i, start());
        }
        return snapshot(i);
    }
    public Map.Entry<K,V> lowerEntry(K key) { return neighbor(key, true, false); }
    public K lowerKey(K key) { return keyOf(lowerEntry(key)); }
    public Map.Entry<K,V> floorEntry(K key) { return neighbor(key, true, true); }
    public K floorKey(K key) { return keyOf(floorEntry(key)); }
    public Map.Entry<K,V> ceilingEntry(K key) { return neighbor(key, false, true); }
    public K ceilingKey(K key) { return keyOf(ceilingEntry(key)); }
    public Map.Entry<K,V> higherEntry(K key) { return neighbor(key, false, false); }
    public K higherKey(K key) { return keyOf(higherEntry(key)); }

    public Set<Map.Entry<K,V>> entrySet() {
        return new AbstractSet<Map.Entry<K,V>>() {
            public int size() { return TreeMap.this.size(); }
            public void clear() { TreeMap.this.clear(); }
            public boolean contains(Object o) {
                if (!(o instanceof Map.Entry)) return false;
                Map.Entry<?,?> e = (Map.Entry<?,?>) o;
                int i = index(e.getKey());
                return i >= 0 && equal(store.nodes.get(i).value, e.getValue());
            }
            public boolean remove(Object o) {
                if (!contains(o)) return false;
                TreeMap.this.remove(((Map.Entry<?,?>) o).getKey());
                return true;
            }
            public Iterator<Map.Entry<K,V>> iterator() {
                return new Iterator<Map.Entry<K,V>>() {
                    int expected = store.modifications;
                    int next = firstIndex();
                    int last = -1;
                    private void checkModification() {
                        if (expected != store.modifications) throw new ConcurrentModificationException();
                    }
                    public boolean hasNext() { return next >= start() && next < end(); }
                    public Map.Entry<K,V> next() {
                        checkModification();
                        if (!hasNext()) throw new NoSuchElementException();
                        last = next;
                        next += descending ? -1 : 1;
                        return store.nodes.get(last);
                    }
                    public void remove() {
                        checkModification();
                        if (last < 0) throw new IllegalStateException();
                        removeAt(last);
                        if (!descending) --next;
                        last = -1;
                        expected = store.modifications;
                    }
                };
            }
        };
    }
    private void endpoint(K key, boolean inclusive) {
        compare(key, key);
        if (hasLower) {
            int c = compare(key, lower);
            if (c < 0 || (c == 0 && inclusive && !lowerInclusive))
                throw new IllegalArgumentException("Endpoint outside map range");
        }
        if (hasUpper) {
            int c = compare(key, upper);
            if (c > 0 || (c == 0 && inclusive && !upperInclusive))
                throw new IllegalArgumentException("Endpoint outside map range");
        }
    }
    public NavigableMap<K,V> descendingMap() {
        return new TreeMap<K,V>(store, !descending, hasLower, lower, lowerInclusive, hasUpper, upper, upperInclusive);
    }
    public NavigableMap<K,V> subMap(K from, boolean fromInclusive, K to, boolean toInclusive) {
        endpoint(from, fromInclusive); endpoint(to, toInclusive);
        if (viewCompare(from, to) > 0) throw new IllegalArgumentException("Reversed map range");
        return descending ? new TreeMap<K,V>(store, true, true, to, toInclusive, true, from, fromInclusive)
                          : new TreeMap<K,V>(store, false, true, from, fromInclusive, true, to, toInclusive);
    }
    public NavigableMap<K,V> headMap(K to, boolean inclusive) {
        endpoint(to, inclusive);
        return descending ? new TreeMap<K,V>(store, true, true, to, inclusive, hasUpper, upper, upperInclusive)
                          : new TreeMap<K,V>(store, false, hasLower, lower, lowerInclusive, true, to, inclusive);
    }
    public NavigableMap<K,V> tailMap(K from, boolean inclusive) {
        endpoint(from, inclusive);
        return descending ? new TreeMap<K,V>(store, true, hasLower, lower, lowerInclusive, true, from, inclusive)
                          : new TreeMap<K,V>(store, false, true, from, inclusive, hasUpper, upper, upperInclusive);
    }
    public SortedMap<K,V> subMap(K from, K to) { return subMap(from, true, to, false); }
    public SortedMap<K,V> headMap(K to) { return headMap(to, false); }
    public SortedMap<K,V> tailMap(K from) { return tailMap(from, true); }
    public NavigableSet<K> navigableKeySet() { return new KeySet<K,V>(this); }
    public NavigableSet<K> descendingKeySet() { return descendingMap().navigableKeySet(); }
    public Set<K> keySet() { return navigableKeySet(); }
    public Object clone() { TreeMap<K,V> copy = new TreeMap<K,V>(comparator()); copy.putAll(this); return copy; }

    private static final class KeySet<K,V> extends AbstractSet<K> implements NavigableSet<K> {
        private final NavigableMap<K,V> map;
        KeySet(NavigableMap<K,V> map) { this.map = map; }
        public int size() { return map.size(); }
        public boolean contains(Object key) { return map.containsKey(key); }
        public boolean remove(Object key) {
            if (!map.containsKey(key)) return false;
            map.remove(key); return true;
        }
        public void clear() { map.clear(); }
        public Iterator<K> iterator() {
            final Iterator<Map.Entry<K,V>> entries = map.entrySet().iterator();
            return new Iterator<K>() {
                public boolean hasNext() { return entries.hasNext(); }
                public K next() { return entries.next().getKey(); }
                public void remove() { entries.remove(); }
            };
        }
        public Iterator<K> descendingIterator() { return descendingSet().iterator(); }
        public Comparator<? super K> comparator() { return map.comparator(); }
        public K first() { return map.firstKey(); }
        public K last() { return map.lastKey(); }
        public K lower(K key) { return map.lowerKey(key); }
        public K floor(K key) { return map.floorKey(key); }
        public K ceiling(K key) { return map.ceilingKey(key); }
        public K higher(K key) { return map.higherKey(key); }
        public K pollFirst() { Map.Entry<K,V> e = map.pollFirstEntry(); return e == null ? null : e.getKey(); }
        public K pollLast() { Map.Entry<K,V> e = map.pollLastEntry(); return e == null ? null : e.getKey(); }
        public NavigableSet<K> descendingSet() { return map.descendingMap().navigableKeySet(); }
        public NavigableSet<K> subSet(K from, boolean fromInclusive, K to, boolean toInclusive) {
            return map.subMap(from, fromInclusive, to, toInclusive).navigableKeySet();
        }
        public NavigableSet<K> headSet(K to, boolean inclusive) { return map.headMap(to, inclusive).navigableKeySet(); }
        public NavigableSet<K> tailSet(K from, boolean inclusive) { return map.tailMap(from, inclusive).navigableKeySet(); }
        public SortedSet<K> subSet(K from, K to) { return subSet(from, true, to, false); }
        public SortedSet<K> headSet(K to) { return headSet(to, false); }
        public SortedSet<K> tailSet(K from) { return tailSet(from, true); }
    }
}
