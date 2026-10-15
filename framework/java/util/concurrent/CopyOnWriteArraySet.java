package java.util.concurrent;

import java.util.AbstractSet;
import java.util.Collection;
import java.util.Iterator;

/**
 * A Set with copy-on-write semantics, backed by CopyOnWriteArrayList.
 *
 * Used for listener registries: reads and iteration never lock, writes copy. Apps
 * and libraries reach for it exactly where a listener list is iterated while
 * callbacks may register more, so a missing class here surfaces as a
 * NoClassDefFoundError inside an unrelated callback.
 *
 * Delegating to CopyOnWriteArrayList rather than reimplementing keeps the snapshot
 * semantics identical — an iterator sees the list as it was when it was created and
 * never throws ConcurrentModificationException, which is the property callers depend
 * on.
 */
public class CopyOnWriteArraySet<E> extends AbstractSet<E> {

    private final CopyOnWriteArrayList<E> mList;

    public CopyOnWriteArraySet() {
        mList = new CopyOnWriteArrayList<E>();
    }

    public CopyOnWriteArraySet(Collection<? extends E> c) {
        mList = new CopyOnWriteArrayList<E>();
        addAll(c);
    }

    @Override
    public int size() {
        return mList.size();
    }

    @Override
    public boolean isEmpty() {
        return mList.isEmpty();
    }

    @Override
    public boolean contains(Object o) {
        return mList.contains(o);
    }

    @Override
    public Iterator<E> iterator() {
        return mList.iterator();
    }

    @Override
    public Object[] toArray() {
        return mList.toArray();
    }

    @Override
    public <T> T[] toArray(T[] a) {
        return mList.toArray(a);
    }

    /**
     * Add only when absent, which is what makes this a Set.
     *
     * Synchronized so a check-then-add cannot interleave with another writer and
     * admit a duplicate; readers are unaffected because they work off the snapshot.
     */
    @Override
    public synchronized boolean add(E e) {
        if (mList.contains(e)) return false;
        return mList.add(e);
    }

    @Override
    public synchronized boolean remove(Object o) {
        return mList.remove(o);
    }

    @Override
    public synchronized boolean addAll(Collection<? extends E> c) {
        if (c == null) return false;
        boolean changed = false;
        for (E e : c) {
            if (add(e)) changed = true;
        }
        return changed;
    }

    @Override
    public synchronized boolean removeAll(Collection<?> c) {
        if (c == null) return false;
        boolean changed = false;
        for (Object o : c) {
            if (remove(o)) changed = true;
        }
        return changed;
    }

    @Override
    public synchronized boolean retainAll(Collection<?> c) {
        if (c == null) return false;
        boolean changed = false;
        // Iterate a snapshot: removing from the live list while walking it would
        // skip elements.
        for (Object o : toArray()) {
            if (!c.contains(o)) {
                if (remove(o)) changed = true;
            }
        }
        return changed;
    }

    @Override
    public synchronized void clear() {
        mList.clear();
    }

    @Override
    public boolean containsAll(Collection<?> c) {
        if (c == null) return true;
        for (Object o : c) {
            if (!contains(o)) return false;
        }
        return true;
    }
}
