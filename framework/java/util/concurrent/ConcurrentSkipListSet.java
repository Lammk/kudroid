package java.util.concurrent;

import java.util.AbstractSet;
import java.util.ArrayList;
import java.util.Collection;
import java.util.Comparator;
import java.util.Iterator;
import java.util.NavigableSet;
import java.util.NoSuchElementException;
import java.util.SortedSet;

/** Sorted concurrent set with shared live range views and snapshot iterators. */
public class ConcurrentSkipListSet<E> extends AbstractSet<E>
        implements NavigableSet<E>, Cloneable, java.io.Serializable {
    private static final long serialVersionUID = -2479143111061671589L;
    private static final class State<E> implements java.io.Serializable {
        private static final long serialVersionUID = 1L;
        final ArrayList<E> values = new ArrayList<E>();
        final Comparator<? super E> comparator;
        State(Comparator<? super E> comparator) { this.comparator = comparator; }
    }
    private final State<E> state;
    private final E low, high;
    private final boolean lowInclusive, highInclusive, descending;

    public ConcurrentSkipListSet() { this((Comparator<? super E>) null); }
    public ConcurrentSkipListSet(Comparator<? super E> comparator) {
        this(new State<E>(comparator), null, false, null, false, false);
    }
    public ConcurrentSkipListSet(Collection<? extends E> c) { this(); addAll(c); }
    public ConcurrentSkipListSet(SortedSet<E> s) { this(s.comparator()); addAll(s); }
    private ConcurrentSkipListSet(State<E> state, E low, boolean li, E high, boolean hi, boolean desc) {
        this.state = state; this.low = low; lowInclusive = li;
        this.high = high; highInclusive = hi; descending = desc;
    }
    @SuppressWarnings("unchecked")
    private int compare(E a, E b) {
        if (a == null || b == null) throw new NullPointerException();
        return state.comparator == null ? ((Comparable<? super E>) a).compareTo(b) : state.comparator.compare(a, b);
    }
    private int viewCompare(E a, E b) { return descending ? compare(b, a) : compare(a, b); }
    private boolean inRange(E e) {
        if (low != null) { int c = compare(e, low); if (c < 0 || (c == 0 && !lowInclusive)) return false; }
        if (high != null) { int c = compare(e, high); if (c > 0 || (c == 0 && !highInclusive)) return false; }
        return true;
    }
    private int index(E e) {
        int a = 0, b = state.values.size();
        while (a < b) { int m = (a + b) >>> 1;
            if (compare(state.values.get(m), e) < 0) a = m + 1; else b = m;
        }
        return a;
    }
    public boolean add(E e) {
        compare(e, e);
        synchronized (state) {
            if (!inRange(e)) throw new IllegalArgumentException("element outside range");
            int i = index(e);
            if (i < state.values.size() && compare(state.values.get(i), e) == 0) return false;
            state.values.add(i, e);
            return true;
        }
    }
    @SuppressWarnings("unchecked")
    public boolean contains(Object o) {
        E e = (E) o; compare(e, e);
        synchronized (state) {
            if (!inRange(e)) return false;
            int i = index(e);
            return i < state.values.size() && compare(state.values.get(i), e) == 0;
        }
    }
    @SuppressWarnings("unchecked")
    public boolean remove(Object o) {
        E e = (E) o; compare(e, e);
        synchronized (state) {
            if (!inRange(e)) return false;
            int i = index(e);
            if (i >= state.values.size() || compare(state.values.get(i), e) != 0) return false;
            state.values.remove(i);
            return true;
        }
    }
    public int size() {
        synchronized (state) { int count = 0; for (E e : state.values) if (inRange(e)) count++; return count; }
    }
    public void clear() {
        synchronized (state) {
            for (int i = state.values.size() - 1; i >= 0; i--) if (inRange(state.values.get(i))) state.values.remove(i);
        }
    }
    private ArrayList<E> snapshot(boolean reverse) {
        synchronized (state) {
            ArrayList<E> out = new ArrayList<E>();
            if (reverse) { for (int i = state.values.size() - 1; i >= 0; i--) {
                E e = state.values.get(i); if (inRange(e)) out.add(e);
            } } else { for (E e : state.values) if (inRange(e)) out.add(e); }
            return out;
        }
    }
    private Iterator<E> iterator(boolean reverse) {
        final Iterator<E> values = snapshot(reverse).iterator();
        return new Iterator<E>() {
            E last;
            boolean removable;
            public boolean hasNext() { return values.hasNext(); }
            public E next() { last = values.next(); removable = true; return last; }
            public void remove() {
                if (!removable) throw new IllegalStateException();
                ConcurrentSkipListSet.this.remove(last);
                removable = false;
            }
        };
    }
    public Iterator<E> iterator() { return iterator(descending); }
    public Iterator<E> descendingIterator() { return iterator(!descending); }
    public Comparator<? super E> comparator() {
        if (!descending) return state.comparator;
        return new Comparator<E>() { public int compare(E a, E b) { return ConcurrentSkipListSet.this.compare(b, a); } };
    }
    private E edge(boolean last, boolean remove) {
        synchronized (state) {
            boolean reverse = descending ^ last;
            for (int i = reverse ? state.values.size() - 1 : 0;
                 i >= 0 && i < state.values.size(); i += reverse ? -1 : 1) {
                E e = state.values.get(i);
                if (inRange(e)) { if (remove) state.values.remove(i); return e; }
            }
            return null;
        }
    }
    public E first() { E e = edge(false, false); if (e == null) throw new NoSuchElementException(); return e; }
    public E last() { E e = edge(true, false); if (e == null) throw new NoSuchElementException(); return e; }
    public E pollFirst() { return edge(false, true); }
    public E pollLast() { return edge(true, true); }
    private E near(E e, boolean below, boolean inclusive) {
        compare(e, e);
        E answer = null;
        synchronized (state) {
            for (E value : state.values) {
                if (!inRange(value)) continue;
                int c = viewCompare(value, e);
                if ((below ? c < 0 : c > 0) || (inclusive && c == 0)) {
                    if (answer == null || (below ? viewCompare(value, answer) > 0 : viewCompare(value, answer) < 0)) answer = value;
                }
            }
        }
        return answer;
    }
    public E lower(E e) { return near(e, true, false); }
    public E floor(E e) { return near(e, true, true); }
    public E ceiling(E e) { return near(e, false, true); }
    public E higher(E e) { return near(e, false, false); }
    private void endpoint(E e, boolean inclusive) {
        compare(e, e);
        if (low != null) { int c = compare(e, low);
            if (c < 0 || (c == 0 && inclusive && !lowInclusive)) throw new IllegalArgumentException(); }
        if (high != null) { int c = compare(e, high);
            if (c > 0 || (c == 0 && inclusive && !highInclusive)) throw new IllegalArgumentException(); }
    }
    public NavigableSet<E> subSet(E from, boolean fi, E to, boolean ti) {
        endpoint(from, fi); endpoint(to, ti);
        if (viewCompare(from, to) > 0) throw new IllegalArgumentException();
        return descending ? new ConcurrentSkipListSet<E>(state, to, ti, from, fi, true) :
                            new ConcurrentSkipListSet<E>(state, from, fi, to, ti, false);
    }
    public NavigableSet<E> headSet(E to, boolean inclusive) {
        endpoint(to, inclusive);
        return descending ? new ConcurrentSkipListSet<E>(state, to, inclusive, high, highInclusive, true) :
                            new ConcurrentSkipListSet<E>(state, low, lowInclusive, to, inclusive, false);
    }
    public NavigableSet<E> tailSet(E from, boolean inclusive) {
        endpoint(from, inclusive);
        return descending ? new ConcurrentSkipListSet<E>(state, low, lowInclusive, from, inclusive, true) :
                            new ConcurrentSkipListSet<E>(state, from, inclusive, high, highInclusive, false);
    }
    public NavigableSet<E> subSet(E from, E to) { return subSet(from, true, to, false); }
    public NavigableSet<E> headSet(E to) { return headSet(to, false); }
    public NavigableSet<E> tailSet(E from) { return tailSet(from, true); }
    public NavigableSet<E> descendingSet() {
        return new ConcurrentSkipListSet<E>(state, low, lowInclusive, high, highInclusive, !descending);
    }
    public ConcurrentSkipListSet<E> clone() {
        ConcurrentSkipListSet<E> copy = new ConcurrentSkipListSet<E>(comparator());
        copy.addAll(this);
        return copy;
    }
}
