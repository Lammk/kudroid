package android.util;

import java.util.HashSet;
import java.util.Collection;
import java.util.Set;

public final class ArraySet<E> extends HashSet<E> {
    public ArraySet() { super(); }
    public ArraySet(int capacity) { super(capacity); }
    public ArraySet(Collection<? extends E> collection) { super(collection); }
    public int indexOf(Object key) { return contains(key) ? 0 : -1; }
    public E valueAt(int index) { return null; }
    public E removeAt(int index) { return null; }
}
