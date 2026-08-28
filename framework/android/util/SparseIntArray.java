package android.util;

import java.util.HashMap;

public class SparseIntArray implements Cloneable {
    private final HashMap<Integer, Integer> mMap = new HashMap<Integer, Integer>();

    public SparseIntArray() {}
    public SparseIntArray(int initialCapacity) {}
    public int get(int key) { return get(key, 0); }
    public int get(int key, int valueIfKeyNotFound) { Integer v = mMap.get(key); return v != null ? v : valueIfKeyNotFound; }
    public void delete(int key) { mMap.remove(key); }
    public void put(int key, int value) { mMap.put(key, value); }
    public int size() { return mMap.size(); }
    public void clear() { mMap.clear(); }
    public void append(int key, int value) { put(key, value); }
}
