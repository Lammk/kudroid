package java.util;

public class HashMap<K, V> implements Map<K, V>, Cloneable, java.io.Serializable {

    private static final int DEFAULT_CAPACITY = 16;

    private Node<K, V>[] table;
    private int size;

    public HashMap() {
        table = new Node[DEFAULT_CAPACITY];
    }

    public HashMap(int initialCapacity) {
        int cap = DEFAULT_CAPACITY;
        while (cap < initialCapacity) {
            cap <<= 1;
        }
        table = new Node[cap];
    }

    public HashMap(int initialCapacity, float loadFactor) {
        this(initialCapacity);
    }

    public HashMap(Map<? extends K, ? extends V> m) {
        this(DEFAULT_CAPACITY);
        putAll(m);
    }

    private static int hashOf(Object key) {
        if (key == null) {
            return 0;
        }
        int h = key.hashCode();
        return h ^ (h >>> 16);
    }

    private int indexFor(int hash) {
        return (hash & 0x7fffffff) % table.length;
    }

    private void resize() {
        Node<K, V>[] old = table;
        table = new Node[old.length * 2];
        size = 0;
        for (int i = 0; i < old.length; i++) {
            Node<K, V> n = old[i];
            while (n != null) {
                put(n.key, n.value);
                n = n.next;
            }
        }
    }

    public int size() {
        return size;
    }

    public boolean isEmpty() {
        return size == 0;
    }

    public V get(Object key) {
        Node<K, V> n = findNode(key);
        return n == null ? null : n.value;
    }

    public V getOrDefault(Object key, V defaultValue) {
        Node<K, V> n = findNode(key);
        return n == null ? defaultValue : n.value;
    }

    private Node<K, V> findNode(Object key) {
        int hash = hashOf(key);
        Node<K, V> n = table[indexFor(hash)];
        while (n != null) {
            if (n.hash == hash && (n.key == key || (key != null && key.equals(n.key)))) {
                return n;
            }
            n = n.next;
        }
        return null;
    }

    public boolean containsKey(Object key) {
        return findNode(key) != null;
    }

    public boolean containsValue(Object value) {
        for (int i = 0; i < table.length; i++) {
            Node<K, V> n = table[i];
            while (n != null) {
                if (value == null ? n.value == null : value.equals(n.value)) {
                    return true;
                }
                n = n.next;
            }
        }
        return false;
    }

    public V put(K key, V value) {
        int hash = hashOf(key);
        int idx = indexFor(hash);
        Node<K, V> n = table[idx];
        while (n != null) {
            if (n.hash == hash && (n.key == key || (key != null && key.equals(n.key)))) {
                V old = n.value;
                n.value = value;
                return old;
            }
            n = n.next;
        }
        Node<K, V> fresh = new Node<K, V>(hash, key, value);
        fresh.next = table[idx];
        table[idx] = fresh;
        size++;
        if (size > table.length * 3 / 4) {
            resize();
        }
        return null;
    }

    public V putIfAbsent(K key, V value) {
        Node<K, V> n = findNode(key);
        if (n != null && n.value != null) {
            return n.value;
        }
        return put(key, value);
    }

    public V remove(Object key) {
        int hash = hashOf(key);
        int idx = indexFor(hash);
        Node<K, V> n = table[idx];
        Node<K, V> prev = null;
        while (n != null) {
            if (n.hash == hash && (n.key == key || (key != null && key.equals(n.key)))) {
                if (prev == null) {
                    table[idx] = n.next;
                } else {
                    prev.next = n.next;
                }
                size--;
                return n.value;
            }
            prev = n;
            n = n.next;
        }
        return null;
    }

    public void putAll(Map<? extends K, ? extends V> m) {
        Iterator<? extends Entry<? extends K, ? extends V>> it = m.entrySet().iterator();
        while (it.hasNext()) {
            Entry<? extends K, ? extends V> e = it.next();
            put(e.getKey(), e.getValue());
        }
    }

    public void clear() {
        for (int i = 0; i < table.length; i++) {
            table[i] = null;
        }
        size = 0;
    }

    public Set<K> keySet() {
        LinkedHashSet<K> out = new LinkedHashSet<K>();
        for (int i = 0; i < table.length; i++) {
            Node<K, V> n = table[i];
            while (n != null) {
                out.add(n.key);
                n = n.next;
            }
        }
        return out;
    }

    public Collection<V> values() {
        ArrayList<V> out = new ArrayList<V>(size);
        for (int i = 0; i < table.length; i++) {
            Node<K, V> n = table[i];
            while (n != null) {
                out.add(n.value);
                n = n.next;
            }
        }
        return out;
    }

    public Set<Entry<K, V>> entrySet() {
        LinkedHashSet<Entry<K, V>> out = new LinkedHashSet<Entry<K, V>>();
        for (int i = 0; i < table.length; i++) {
            Node<K, V> n = table[i];
            while (n != null) {
                out.add(n);
                n = n.next;
            }
        }
        return out;
    }

    public Object clone() {
        return new HashMap<K, V>(this);
    }

    public String toString() {
        StringBuilder sb = new StringBuilder("{");
        boolean first = true;
        for (int i = 0; i < table.length; i++) {
            Node<K, V> n = table[i];
            while (n != null) {
                if (!first) {
                    sb.append(", ");
                }
                first = false;
                sb.append(n.key).append('=').append(n.value);
                n = n.next;
            }
        }
        return sb.append('}').toString();
    }

    private static final class Node<K, V> implements Entry<K, V> {

        final int hash;
        final K key;
        V value;
        Node<K, V> next;

        Node(int hash, K key, V value) {
            this.hash = hash;
            this.key = key;
            this.value = value;
        }

        public K getKey() {
            return key;
        }

        public V getValue() {
            return value;
        }

        public V setValue(V newValue) {
            V old = value;
            value = newValue;
            return old;
        }

        public boolean equals(Object other) {
            if (!(other instanceof Entry)) {
                return false;
            }
            Entry<?, ?> e = (Entry<?, ?>) other;
            Object ok = e.getKey();
            Object ov = e.getValue();
            return (key == null ? ok == null : key.equals(ok))
                    && (value == null ? ov == null : value.equals(ov));
        }

        public int hashCode() {
            return (key == null ? 0 : key.hashCode()) ^ (value == null ? 0 : value.hashCode());
        }

        public String toString() {
            return key + "=" + value;
        }
    }
}
