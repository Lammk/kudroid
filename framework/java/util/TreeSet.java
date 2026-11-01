package java.util;

import java.io.Serializable;

public class TreeSet<E> extends AbstractSet<E> implements NavigableSet<E>, Cloneable, Serializable {
    private static final long serialVersionUID = -2479143000061671589L;
    private final NavigableMap<E,Object> m;
    private static final Object PRESENT = new Object();

    TreeSet(NavigableMap<E,Object> m) { this.m = m; }
    public TreeSet() { this(new TreeMap<E,Object>()); }
    public TreeSet(Comparator<? super E> comparator) { this(new TreeMap<E,Object>(comparator)); }
    public TreeSet(Collection<? extends E> c) {
        this();
        addAll(c);
    }
    public TreeSet(SortedSet<E> s) {
        this(s.comparator());
        addAll(s);
    }

    public Iterator<E> iterator() { return m.navigableKeySet().iterator(); }
    public Iterator<E> descendingIterator() { return m.descendingKeySet().iterator(); }
    public NavigableSet<E> descendingSet() { return new TreeSet<E>(m.descendingMap()); }
    public int size() { return m.size(); }
    public boolean isEmpty() { return m.isEmpty(); }
    public boolean contains(Object o) { return m.containsKey(o); }
    public boolean add(E e) { return m.put(e, PRESENT) == null; }
    public boolean remove(Object o) { return m.remove(o) == PRESENT; }
    public void clear() { m.clear(); }

    public Comparator<? super E> comparator() { return m.comparator(); }
    public E first() { return m.firstKey(); }
    public E last() { return m.lastKey(); }
    public E lower(E e) { return m.lowerKey(e); }
    public E floor(E e) { return m.floorKey(e); }
    public E ceiling(E e) { return m.ceilingKey(e); }
    public E higher(E e) { return m.higherKey(e); }
    public E pollFirst() { Map.Entry<E,Object> e = m.pollFirstEntry(); return e == null ? null : e.getKey(); }
    public E pollLast() { Map.Entry<E,Object> e = m.pollLastEntry(); return e == null ? null : e.getKey(); }
    public NavigableSet<E> subSet(E fromElement, boolean fromInclusive, E toElement, boolean toInclusive) {
        return new TreeSet<E>(m.subMap(fromElement, fromInclusive, toElement, toInclusive));
    }
    public NavigableSet<E> headSet(E toElement, boolean inclusive) { return new TreeSet<E>(m.headMap(toElement, inclusive)); }
    public NavigableSet<E> tailSet(E fromElement, boolean inclusive) { return new TreeSet<E>(m.tailMap(fromElement, inclusive)); }
    public SortedSet<E> subSet(E fromElement, E toElement) { return subSet(fromElement, true, toElement, false); }
    public SortedSet<E> headSet(E toElement) { return headSet(toElement, false); }
    public SortedSet<E> tailSet(E fromElement) { return tailSet(fromElement, true); }
    public Object clone() { return new TreeSet<E>((SortedSet<E>) this); }
}
