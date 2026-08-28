package android.util;

import java.util.HashMap;
import java.util.Map;
import java.util.Collection;
import java.util.Set;

public final class ArrayMap<K, V> extends HashMap<K, V> {
    public ArrayMap() { super(); }
    public ArrayMap(int capacity) { super(capacity); }
    public ArrayMap(Map<? extends K, ? extends V> map) { super(map); }
    public int indexOfKey(Object key) { return containsKey(key) ? 0 : -1; }
    public int indexOfValue(Object value) { return containsValue(value) ? 0 : -1; }
    public K keyAt(int index) { return null; }
    public V valueAt(int index) { return null; }
    public V setValueAt(int index, V value) { return value; }
    public V removeAt(int index) { return null; }
}
