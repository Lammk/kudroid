package java.util;

public class LinkedHashMap<K, V> extends HashMap<K, V> {
    private boolean accessOrder = false;

    public LinkedHashMap() {
    }

    public LinkedHashMap(int initialCapacity) {
        super(initialCapacity);
    }

    public LinkedHashMap(int initialCapacity, float loadFactor) {
        super(initialCapacity, loadFactor);
    }

    public LinkedHashMap(int initialCapacity, float loadFactor, boolean accessOrder) {
        super(initialCapacity, loadFactor);
        this.accessOrder = accessOrder;
    }

    public LinkedHashMap(Map<? extends K, ? extends V> m) {
        super(m);
    }

    public Map.Entry<K, V> eldest() {
        Iterator<Map.Entry<K, V>> it = entrySet().iterator();
        return it.hasNext() ? it.next() : null;
    }
}
