package java.util.concurrent;

/** Bản sao khi ghi: iterate không cần khoá, dùng nhiều cho danh sách listener. */
public class CopyOnWriteArrayList<E> implements java.util.List<E> {

    private volatile Object[] elements = new Object[0];

    public CopyOnWriteArrayList() {
    }

    public CopyOnWriteArrayList(java.util.Collection<? extends E> c) {
        elements = c.toArray();
    }

    public int size() {
        return elements.length;
    }

    public boolean isEmpty() {
        return elements.length == 0;
    }

    public E get(int index) {
        Object[] snapshot = elements;
        if (index < 0 || index >= snapshot.length) {
            throw new IndexOutOfBoundsException("index " + index);
        }
        return (E) snapshot[index];
    }

    public synchronized E set(int index, E element) {
        Object[] next = copy(elements, elements.length);
        E old = (E) next[index];
        next[index] = element;
        elements = next;
        return old;
    }

    public synchronized boolean add(E e) {
        Object[] next = copy(elements, elements.length + 1);
        next[elements.length] = e;
        elements = next;
        return true;
    }

    public synchronized void add(int index, E element) {
        Object[] old = elements;
        Object[] next = new Object[old.length + 1];
        System.arraycopy(old, 0, next, 0, index);
        next[index] = element;
        System.arraycopy(old, index, next, index + 1, old.length - index);
        elements = next;
    }

    public synchronized boolean addIfAbsent(E e) {
        if (contains(e)) {
            return false;
        }
        return add(e);
    }

    public synchronized E remove(int index) {
        Object[] old = elements;
        E removed = (E) old[index];
        Object[] next = new Object[old.length - 1];
        System.arraycopy(old, 0, next, 0, index);
        System.arraycopy(old, index + 1, next, index, old.length - index - 1);
        elements = next;
        return removed;
    }

    public synchronized boolean remove(Object o) {
        int idx = indexOf(o);
        if (idx < 0) {
            return false;
        }
        remove(idx);
        return true;
    }

    public synchronized void clear() {
        elements = new Object[0];
    }

    public int indexOf(Object o) {
        Object[] snapshot = elements;
        for (int i = 0; i < snapshot.length; i++) {
            if (o == null ? snapshot[i] == null : o.equals(snapshot[i])) {
                return i;
            }
        }
        return -1;
    }

    public int lastIndexOf(Object o) {
        Object[] snapshot = elements;
        for (int i = snapshot.length - 1; i >= 0; i--) {
            if (o == null ? snapshot[i] == null : o.equals(snapshot[i])) {
                return i;
            }
        }
        return -1;
    }

    public boolean contains(Object o) {
        return indexOf(o) >= 0;
    }

    public boolean containsAll(java.util.Collection<?> c) {
        java.util.Iterator<?> it = c.iterator();
        while (it.hasNext()) {
            if (!contains(it.next())) {
                return false;
            }
        }
        return true;
    }

    public synchronized boolean addAll(java.util.Collection<? extends E> c) {
        Object[] items = c.toArray();
        if (items.length == 0) {
            return false;
        }
        Object[] next = copy(elements, elements.length + items.length);
        System.arraycopy(items, 0, next, elements.length, items.length);
        elements = next;
        return true;
    }

    public synchronized boolean removeAll(java.util.Collection<?> c) {
        java.util.ArrayList<E> keep = new java.util.ArrayList<E>();
        Object[] snapshot = elements;
        for (int i = 0; i < snapshot.length; i++) {
            if (!c.contains(snapshot[i])) {
                keep.add((E) snapshot[i]);
            }
        }
        boolean changed = keep.size() != snapshot.length;
        elements = keep.toArray();
        return changed;
    }

    public synchronized boolean retainAll(java.util.Collection<?> c) {
        java.util.ArrayList<E> keep = new java.util.ArrayList<E>();
        Object[] snapshot = elements;
        for (int i = 0; i < snapshot.length; i++) {
            if (c.contains(snapshot[i])) {
                keep.add((E) snapshot[i]);
            }
        }
        boolean changed = keep.size() != snapshot.length;
        elements = keep.toArray();
        return changed;
    }

    public Object[] toArray() {
        return copy(elements, elements.length);
    }

    public <T> T[] toArray(T[] a) {
        Object[] snapshot = elements;
        if (a.length < snapshot.length) {
            T[] fresh = (T[]) java.lang.reflect.Array.newInstance(
                    a.getClass().getComponentType(), snapshot.length);
            System.arraycopy(snapshot, 0, fresh, 0, snapshot.length);
            return fresh;
        }
        System.arraycopy(snapshot, 0, a, 0, snapshot.length);
        return a;
    }

    public java.util.List<E> subList(int fromIndex, int toIndex) {
        java.util.ArrayList<E> out = new java.util.ArrayList<E>();
        Object[] snapshot = elements;
        for (int i = fromIndex; i < toIndex; i++) {
            out.add((E) snapshot[i]);
        }
        return out;
    }

    public java.util.Iterator<E> iterator() {
        return listIterator();
    }

    public java.util.ListIterator<E> listIterator() {
        return new Snapshot(elements);
    }

    public String toString() {
        return java.util.Arrays.toString(elements);
    }

    private static Object[] copy(Object[] src, int newLength) {
        Object[] out = new Object[newLength];
        System.arraycopy(src, 0, out, 0, Math.min(src.length, newLength));
        return out;
    }

    private final class Snapshot implements java.util.ListIterator<E> {

        private final Object[] snapshot;
        private int cursor;

        Snapshot(Object[] snapshot) {
            this.snapshot = snapshot;
        }

        public boolean hasNext() {
            return cursor < snapshot.length;
        }

        public E next() {
            if (cursor >= snapshot.length) {
                throw new java.util.NoSuchElementException();
            }
            return (E) snapshot[cursor++];
        }

        public boolean hasPrevious() {
            return cursor > 0;
        }

        public E previous() {
            if (cursor <= 0) {
                throw new java.util.NoSuchElementException();
            }
            return (E) snapshot[--cursor];
        }

        public int nextIndex() {
            return cursor;
        }

        public int previousIndex() {
            return cursor - 1;
        }

        public void remove() {
            throw new UnsupportedOperationException();
        }

        public void set(E e) {
            throw new UnsupportedOperationException();
        }

        public void add(E e) {
            throw new UnsupportedOperationException();
        }
    }
}
