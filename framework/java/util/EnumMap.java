package java.util;

import java.io.Serializable;

public class EnumMap<K extends Enum<K>, V> extends AbstractMap<K, V> implements Serializable, Cloneable {
    private static final long serialVersionUID = 4586612400691928654L;
    private final Class<K> keyType;
    private final Map<K, V> map = new HashMap<K, V>();

    public EnumMap(Class<K> keyType) {
        this.keyType = keyType;
    }
    public EnumMap(EnumMap<K, ? extends V> m) {
        this.keyType = m.keyType;
        this.map.putAll(m.map);
    }
    public EnumMap(Map<K, ? extends V> m) {
        if (m instanceof EnumMap) {
            EnumMap<K, ? extends V> em = (EnumMap<K, ? extends V>) m;
            this.keyType = em.keyType;
            this.map.putAll(em.map);
        } else {
            if (m.isEmpty()) throw new IllegalArgumentException("Specified map is empty");
            this.keyType = m.keySet().iterator().next().getDeclaringClass();
            this.map.putAll(m);
        }
    }
    public int size() { return map.size(); }
    public boolean containsValue(Object value) { return map.containsValue(value); }
    public boolean containsKey(Object key) { return map.containsKey(key); }
    public V get(Object key) { return map.get(key); }
    public V put(K key, V value) { return map.put(key, value); }
    public V remove(Object key) { return map.remove(key); }
    public void putAll(Map<? extends K, ? extends V> m) { map.putAll(m); }
    public void clear() { map.clear(); }
    public Set<Map.Entry<K, V>> entrySet() { return map.entrySet(); }
}
