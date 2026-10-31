package java.util;

public abstract class AbstractMap<K,V> implements Map<K,V> {
    protected AbstractMap() {}
    public int size() { return entrySet().size(); }
    public boolean isEmpty() { return size() == 0; }
    public boolean containsValue(Object value) {
        Iterator<Entry<K,V>> i = entrySet().iterator();
        if (value==null) {
            while (i.hasNext()) {
                Entry<K,V> e = i.next();
                if (e.getValue()==null) return true;
            }
        } else {
            while (i.hasNext()) {
                Entry<K,V> e = i.next();
                if (value.equals(e.getValue())) return true;
            }
        }
        return false;
    }
    public boolean containsKey(Object key) {
        Iterator<Map.Entry<K,V>> i = entrySet().iterator();
        if (key==null) {
            while (i.hasNext()) {
                Entry<K,V> e = i.next();
                if (e.getKey()==null) return true;
            }
        } else {
            while (i.hasNext()) {
                Entry<K,V> e = i.next();
                if (key.equals(e.getKey())) return true;
            }
        }
        return false;
    }
    public V get(Object key) {
        Iterator<Entry<K,V>> i = entrySet().iterator();
        if (key==null) {
            while (i.hasNext()) {
                Entry<K,V> e = i.next();
                if (e.getKey()==null) return e.getValue();
            }
        } else {
            while (i.hasNext()) {
                Entry<K,V> e = i.next();
                if (key.equals(e.getKey())) return e.getValue();
            }
        }
        return null;
    }
    public V put(K key, V value) { throw new UnsupportedOperationException(); }
    public V remove(Object key) {
        Iterator<Entry<K,V>> i = entrySet().iterator();
        Entry<K,V> correctEntry = null;
        if (key==null) {
            while (correctEntry==null && i.hasNext()) {
                Entry<K,V> e = i.next();
                if (e.getKey()==null) correctEntry = e;
            }
        } else {
            while (correctEntry==null && i.hasNext()) {
                Entry<K,V> e = i.next();
                if (key.equals(e.getKey())) correctEntry = e;
            }
        }
        V oldValue = null;
        if (correctEntry !=null) {
            oldValue = correctEntry.getValue();
            i.remove();
        }
        return oldValue;
    }
    public void putAll(Map<? extends K, ? extends V> m) {
        for (Map.Entry<? extends K, ? extends V> e : m.entrySet())
            put(e.getKey(), e.getValue());
    }
    public void clear() { entrySet().clear(); }
    public Set<K> keySet() {
        return new AbstractSet<K>() {
            public Iterator<K> iterator() {
                return new Iterator<K>() {
                    private Iterator<Entry<K,V>> i = entrySet().iterator();
                    public boolean hasNext() { return i.hasNext(); }
                    public K next() { return i.next().getKey(); }
                    public void remove() { i.remove(); }
                };
            }
            public int size() { return AbstractMap.this.size(); }
            public boolean isEmpty() { return AbstractMap.this.isEmpty(); }
            public void clear() { AbstractMap.this.clear(); }
            public boolean contains(Object k) { return AbstractMap.this.containsKey(k); }
        };
    }
    public Collection<V> values() {
        return new AbstractCollection<V>() {
            public Iterator<V> iterator() {
                return new Iterator<V>() {
                    private Iterator<Entry<K,V>> i = entrySet().iterator();
                    public boolean hasNext() { return i.hasNext(); }
                    public V next() { return i.next().getValue(); }
                    public void remove() { i.remove(); }
                };
            }
            public int size() { return AbstractMap.this.size(); }
            public boolean isEmpty() { return AbstractMap.this.isEmpty(); }
            public void clear() { AbstractMap.this.clear(); }
            public boolean contains(Object v) { return AbstractMap.this.containsValue(v); }
        };
    }
    public abstract Set<Entry<K,V>> entrySet();

    public static class SimpleImmutableEntry<K,V> implements Entry<K,V>, java.io.Serializable {
        private static final long serialVersionUID = 7138329143949025153L;
        private final K key;
        private final V value;

        public SimpleImmutableEntry(K key, V value) {
            this.key = key;
            this.value = value;
        }

        public SimpleImmutableEntry(Entry<? extends K, ? extends V> entry) {
            this.key = entry.getKey();
            this.value = entry.getValue();
        }

        public K getKey() { return key; }
        public V getValue() { return value; }
        public V setValue(V value) { throw new UnsupportedOperationException(); }

        public boolean equals(Object object) {
            if (!(object instanceof Entry)) return false;
            Entry<?,?> entry = (Entry<?,?>) object;
            return (key == null ? entry.getKey() == null : key.equals(entry.getKey()))
                    && (value == null ? entry.getValue() == null : value.equals(entry.getValue()));
        }

        public int hashCode() {
            return (key == null ? 0 : key.hashCode()) ^ (value == null ? 0 : value.hashCode());
        }

        public String toString() { return key + "=" + value; }
    }
}
