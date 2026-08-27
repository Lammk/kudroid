package java.util;

public class ArrayList<E> implements List<E>, Cloneable, java.io.Serializable {

    private Object[] elements;
    private int size;

    public ArrayList() {
        elements = new Object[10];
    }

    public ArrayList(int initialCapacity) {
        elements = new Object[initialCapacity < 1 ? 10 : initialCapacity];
    }

    public ArrayList(Collection<? extends E> c) {
        Object[] items = c.toArray();
        elements = new Object[items.length + 10];
        System.arraycopy(items, 0, elements, 0, items.length);
        size = items.length;
    }

    private void ensure(int minCapacity) {
        if (minCapacity <= elements.length) {
            return;
        }
        int newLen = elements.length * 2 + 2;
        if (newLen < minCapacity) {
            newLen = minCapacity;
        }
        Object[] next = new Object[newLen];
        System.arraycopy(elements, 0, next, 0, size);
        elements = next;
    }

    private void checkIndex(int index) {
        if (index < 0 || index >= size) {
            throw new IndexOutOfBoundsException("index " + index + ", size " + size);
        }
    }

    public int size() {
        return size;
    }

    public boolean isEmpty() {
        return size == 0;
    }

    public E get(int index) {
        checkIndex(index);
        return (E) elements[index];
    }

    public E set(int index, E element) {
        checkIndex(index);
        E old = (E) elements[index];
        elements[index] = element;
        return old;
    }

    public boolean add(E e) {
        ensure(size + 1);
        elements[size++] = e;
        return true;
    }

    public void add(int index, E element) {
        if (index < 0 || index > size) {
            throw new IndexOutOfBoundsException("index " + index);
        }
        ensure(size + 1);
        System.arraycopy(elements, index, elements, index + 1, size - index);
        elements[index] = element;
        size++;
    }

    public E remove(int index) {
        checkIndex(index);
        E old = (E) elements[index];
        System.arraycopy(elements, index + 1, elements, index, size - index - 1);
        elements[--size] = null;
        return old;
    }

    public boolean remove(Object o) {
        int idx = indexOf(o);
        if (idx < 0) {
            return false;
        }
        remove(idx);
        return true;
    }

    public void clear() {
        for (int i = 0; i < size; i++) {
            elements[i] = null;
        }
        size = 0;
    }

    public int indexOf(Object o) {
        for (int i = 0; i < size; i++) {
            if (o == null ? elements[i] == null : o.equals(elements[i])) {
                return i;
            }
        }
        return -1;
    }

    public int lastIndexOf(Object o) {
        for (int i = size - 1; i >= 0; i--) {
            if (o == null ? elements[i] == null : o.equals(elements[i])) {
                return i;
            }
        }
        return -1;
    }

    public boolean contains(Object o) {
        return indexOf(o) >= 0;
    }

    public boolean containsAll(Collection<?> c) {
        Iterator<?> it = c.iterator();
        while (it.hasNext()) {
            if (!contains(it.next())) {
                return false;
            }
        }
        return true;
    }

    public boolean addAll(Collection<? extends E> c) {
        Object[] items = c.toArray();
        if (items.length == 0) {
            return false;
        }
        ensure(size + items.length);
        System.arraycopy(items, 0, elements, size, items.length);
        size += items.length;
        return true;
    }

    public boolean addAll(int index, Collection<? extends E> c) {
        Object[] items = c.toArray();
        if (items.length == 0) {
            return false;
        }
        ensure(size + items.length);
        System.arraycopy(elements, index, elements, index + items.length, size - index);
        System.arraycopy(items, 0, elements, index, items.length);
        size += items.length;
        return true;
    }

    public boolean removeAll(Collection<?> c) {
        boolean changed = false;
        for (int i = size - 1; i >= 0; i--) {
            if (c.contains(elements[i])) {
                remove(i);
                changed = true;
            }
        }
        return changed;
    }

    public boolean retainAll(Collection<?> c) {
        boolean changed = false;
        for (int i = size - 1; i >= 0; i--) {
            if (!c.contains(elements[i])) {
                remove(i);
                changed = true;
            }
        }
        return changed;
    }

    public Object[] toArray() {
        Object[] out = new Object[size];
        System.arraycopy(elements, 0, out, 0, size);
        return out;
    }

    public <T> T[] toArray(T[] a) {
        if (a.length < size) {
            T[] fresh = (T[]) java.lang.reflect.Array.newInstance(
                    a.getClass().getComponentType(), size);
            System.arraycopy(elements, 0, fresh, 0, size);
            return fresh;
        }
        System.arraycopy(elements, 0, a, 0, size);
        for (int i = size; i < a.length; i++) {
            a[i] = null;
        }
        return a;
    }

    public List<E> subList(int fromIndex, int toIndex) {
        ArrayList<E> out = new ArrayList<E>(toIndex - fromIndex);
        for (int i = fromIndex; i < toIndex; i++) {
            out.add((E) elements[i]);
        }
        return out;
    }

    public void ensureCapacity(int minCapacity) {
        ensure(minCapacity);
    }

    public void trimToSize() {
    }

    public Object clone() {
        return new ArrayList<E>(this);
    }

    public Iterator<E> iterator() {
        return listIterator();
    }

    public ListIterator<E> listIterator() {
        return new Itr();
    }

    public void sort(Comparator<? super E> c) {
        Collections.sort(this, c);
    }

    public boolean equals(Object other) {
        if (this == other) {
            return true;
        }
        if (!(other instanceof List)) {
            return false;
        }
        List<?> o = (List<?>) other;
        if (o.size() != size) {
            return false;
        }
        for (int i = 0; i < size; i++) {
            Object a = elements[i];
            Object b = o.get(i);
            if (a == null ? b != null : !a.equals(b)) {
                return false;
            }
        }
        return true;
    }

    public int hashCode() {
        int h = 1;
        for (int i = 0; i < size; i++) {
            h = 31 * h + (elements[i] == null ? 0 : elements[i].hashCode());
        }
        return h;
    }

    public String toString() {
        StringBuilder sb = new StringBuilder("[");
        for (int i = 0; i < size; i++) {
            if (i > 0) {
                sb.append(", ");
            }
            sb.append(elements[i]);
        }
        return sb.append(']').toString();
    }

    private final class Itr implements ListIterator<E> {

        private int cursor;
        private int last = -1;

        public boolean hasNext() {
            return cursor < size;
        }

        public E next() {
            if (cursor >= size) {
                throw new NoSuchElementException();
            }
            last = cursor;
            return (E) elements[cursor++];
        }

        public boolean hasPrevious() {
            return cursor > 0;
        }

        public E previous() {
            if (cursor <= 0) {
                throw new NoSuchElementException();
            }
            last = --cursor;
            return (E) elements[cursor];
        }

        public int nextIndex() {
            return cursor;
        }

        public int previousIndex() {
            return cursor - 1;
        }

        public void remove() {
            if (last < 0) {
                throw new IllegalStateException("next() has not been called");
            }
            ArrayList.this.remove(last);
            cursor = last;
            last = -1;
        }

        public void set(E e) {
            if (last < 0) {
                throw new IllegalStateException("next() has not been called");
            }
            elements[last] = e;
        }

        public void add(E e) {
            ArrayList.this.add(cursor++, e);
            last = -1;
        }
    }
}
