package java.util;

public abstract class AbstractList<E> extends AbstractCollection<E> implements List<E> {
    protected transient int modCount = 0;
    protected AbstractList() {}
    public boolean add(E e) {
        add(size(), e);
        return true;
    }
    public abstract E get(int index);
    public E set(int index, E element) { throw new UnsupportedOperationException(); }
    public void add(int index, E element) { throw new UnsupportedOperationException(); }
    public E remove(int index) { throw new UnsupportedOperationException(); }
    public int indexOf(Object o) {
        ListIterator<E> it = listIterator();
        if (o==null) {
            while (it.hasNext())
                if (it.next()==null)
                    return it.previousIndex();
        } else {
            while (it.hasNext())
                if (o.equals(it.next()))
                    return it.previousIndex();
        }
        return -1;
    }
    public int lastIndexOf(Object o) {
        ListIterator<E> it = listIterator(size());
        if (o==null) {
            while (it.hasPrevious())
                if (it.previous()==null)
                    return it.nextIndex();
        } else {
            while (it.hasPrevious())
                if (o.equals(it.previous()))
                    return it.nextIndex();
        }
        return -1;
    }
    public void clear() {
        removeRange(0, size());
    }
    public boolean addAll(int index, Collection<? extends E> c) {
        boolean modified = false;
        for (E e : c) {
            add(index++, e);
            modified = true;
        }
        return modified;
    }
    public Iterator<E> iterator() { return listIterator(); }
    public ListIterator<E> listIterator() { return listIterator(0); }
    public ListIterator<E> listIterator(final int index) {
        if (index < 0 || index > size()) throw new IndexOutOfBoundsException();
        return new ListIterator<E>() {
            int cursor = index;
            int lastRet = -1;
            public boolean hasNext() { return cursor < size(); }
            public E next() {
                int i = cursor;
                E next = get(i);
                lastRet = i;
                cursor = i + 1;
                return next;
            }
            public boolean hasPrevious() { return cursor > 0; }
            public E previous() {
                int i = cursor - 1;
                E prev = get(i);
                lastRet = cursor = i;
                return prev;
            }
            public int nextIndex() { return cursor; }
            public int previousIndex() { return cursor - 1; }
            public void remove() {
                if (lastRet < 0) throw new IllegalStateException();
                AbstractList.this.remove(lastRet);
                cursor = lastRet;
                lastRet = -1;
            }
            public void set(E e) {
                if (lastRet < 0) throw new IllegalStateException();
                AbstractList.this.set(lastRet, e);
            }
            public void add(E e) {
                int i = cursor;
                AbstractList.this.add(i, e);
                lastRet = -1;
                cursor = i + 1;
            }
        };
    }
    public List<E> subList(int fromIndex, int toIndex) {
        return new ArrayList<E>(this).subList(fromIndex, toIndex);
    }
    protected void removeRange(int fromIndex, int toIndex) {
        ListIterator<E> it = listIterator(fromIndex);
        for (int i=0, n=toIndex-fromIndex; i<n; i++) {
            it.next();
            it.remove();
        }
    }
}
