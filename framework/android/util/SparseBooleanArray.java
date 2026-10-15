package android.util;

import java.util.HashMap;

public class SparseBooleanArray implements Cloneable {
    private final HashMap<Integer, Boolean> mMap = new HashMap<Integer, Boolean>();

    public SparseBooleanArray() {}
    public SparseBooleanArray(int initialCapacity) {}
    public boolean get(int key) { return get(key, false); }
    public boolean get(int key, boolean valueIfKeyNotFound) { Boolean v = mMap.get(key); return v != null ? v : valueIfKeyNotFound; }
    public void delete(int key) { mMap.remove(key); }
    public void put(int key, boolean value) { mMap.put(key, value); }
    public int size() { return mMap.size(); }
    public void clear() { mMap.clear(); }
    public void append(int key, boolean value) { put(key, value); }
}
