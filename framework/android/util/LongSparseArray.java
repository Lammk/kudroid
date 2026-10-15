package android.util;

import java.util.HashMap;

public class LongSparseArray<E> implements Cloneable {
    private final HashMap<Long, E> mMap = new HashMap<Long, E>();

    public LongSparseArray() {}
    public LongSparseArray(int initialCapacity) {}
    public E get(long key) { return mMap.get(key); }
    public E get(long key, E valueIfKeyNotFound) { E v = mMap.get(key); return v != null ? v : valueIfKeyNotFound; }
    public void delete(long key) { mMap.remove(key); }
    public void put(long key, E value) { mMap.put(key, value); }
    public int size() { return mMap.size(); }
    public void clear() { mMap.clear(); }
    public void append(long key, E value) { put(key, value); }
}
