package java.util;

/**
 * Set based on ArrayList keeps insertion order. Order O(n) for contains but framework
 * Only use sets with a few dozen elements.
 */
public class HashSet<E> implements Set<E>, Cloneable, java.io.Serializable {

    private final ArrayList<E> items;

    public HashSet() {
        items = new ArrayList<E>();
    }

    public HashSet(int initialCapacity) {
        items = new ArrayList<E>(initialCapacity);
    }

    public HashSet(Collection<? extends E> c) {
        items = new ArrayList<E>();
        addAll(c);
    }

    public int size() {
        return items.size();
    }

    public boolean isEmpty() {
        return items.isEmpty();
    }

    public boolean contains(Object o) {
        return items.contains(o);
    }

    public boolean add(E e) {
        if (items.contains(e)) {
            return false;
        }
        return items.add(e);
    }

    public boolean remove(Object o) {
        return items.remove(o);
    }

    public void clear() {
        items.clear();
    }

    public boolean containsAll(Collection<?> c) {
        return items.containsAll(c);
    }

    public boolean addAll(Collection<? extends E> c) {
        boolean changed = false;
        Iterator<? extends E> it = c.iterator();
        while (it.hasNext()) {
            if (add(it.next())) {
                changed = true;
            }
        }
        return changed;
    }

    public boolean removeAll(Collection<?> c) {
        return items.removeAll(c);
    }

    public boolean retainAll(Collection<?> c) {
        return items.retainAll(c);
    }

    public Object[] toArray() {
        return items.toArray();
    }

    public <T> T[] toArray(T[] a) {
        return items.toArray(a);
    }

    public Iterator<E> iterator() {
        return items.iterator();
    }

    public Object clone() {
        return new HashSet<E>(this);
    }

    public boolean equals(Object other) {
        if (this == other) {
            return true;
        }
        if (!(other instanceof Set)) {
            return false;
        }
        Set<?> o = (Set<?>) other;
        return o.size() == size() && containsAll(o);
    }

    public int hashCode() {
        int h = 0;
        Iterator<E> it = iterator();
        while (it.hasNext()) {
            E e = it.next();
            h += e == null ? 0 : e.hashCode();
        }
        return h;
    }

    public String toString() {
        return items.toString();
    }
}
