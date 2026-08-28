package android.util;

import java.util.HashMap;

public class SparseLongArray implements Cloneable {
    private final HashMap<Integer, Long> mMap = new HashMap<Integer, Long>();

    public SparseLongArray() {}
    public SparseLongArray(int initialCapacity) {}
    public long get(int key) { return get(key, 0L); }
    public long get(int key, long valueIfKeyNotFound) { Long v = mMap.get(key); return v != null ? v : valueIfKeyNotFound; }
    public void delete(int key) { mMap.remove(key); }
    public void put(int key, long value) { mMap.put(key, value); }
    public int size() { return mMap.size(); }
    public void clear() { mMap.clear(); }
    public void append(int key, long value) { put(key, value); }
}
