package android.util;

import java.util.HashMap;

public class SparseArray<E> implements Cloneable {
    private final HashMap<Integer, E> mMap = new HashMap<Integer, E>();

    public SparseArray() {}
    public SparseArray(int initialCapacity) {}
    public E get(int key) { return mMap.get(key); }
    public E get(int key, E valueIfKeyNotFound) { E v = mMap.get(key); return v != null ? v : valueIfKeyNotFound; }
    public void delete(int key) { mMap.remove(key); }
    public void remove(int key) { delete(key); }
    public void put(int key, E value) { mMap.put(key, value); }
    public int size() { return mMap.size(); }
    public int keyAt(int index) { return 0; }
    public E valueAt(int index) { return null; }
    public void clear() { mMap.clear(); }
    public void append(int key, E value) { put(key, value); }
    public int indexOfKey(int key) { return mMap.containsKey(key) ? 0 : -1; }
}
