package java.util;

public class ArrayDeque<E> implements Deque<E> {
    private Object[] elements;
    private int head;
    private int tail;

    public ArrayDeque() {
        elements = new Object[16];
    }

    public ArrayDeque(int numElements) {
        int initialCapacity = 8;
        if (numElements >= initialCapacity) {
            initialCapacity = numElements;
            initialCapacity |= (initialCapacity >>> 1);
            initialCapacity |= (initialCapacity >>> 2);
            initialCapacity |= (initialCapacity >>> 4);
            initialCapacity |= (initialCapacity >>> 8);
            initialCapacity |= (initialCapacity >>> 16);
            initialCapacity++;
            if (initialCapacity < 0) initialCapacity >>>= 1;
        }
        elements = new Object[initialCapacity];
    }

    private void doubleCapacity() {
        int p = head;
        int n = elements.length;
        int r = n - p;
        int newCapacity = n << 1;
        if (newCapacity < 0) throw new IllegalStateException("Deque too big");
        Object[] a = new Object[newCapacity];
        System.arraycopy(elements, p, a, 0, r);
        System.arraycopy(elements, 0, a, r, p);
        elements = a;
        head = 0;
        tail = n;
    }

    @Override
    public void addFirst(E e) {
        if (e == null) throw new NullPointerException();
        head = (head - 1) & (elements.length - 1);
        elements[head] = e;
        if (head == tail) doubleCapacity();
    }

    @Override
    public void addLast(E e) {
        if (e == null) throw new NullPointerException();
        elements[tail] = e;
        tail = (tail + 1) & (elements.length - 1);
        if (tail == head) doubleCapacity();
    }

    @Override
    public boolean offerFirst(E e) {
        addFirst(e);
        return true;
    }

    @Override
    public boolean offerLast(E e) {
        addLast(e);
        return true;
    }

    @Override
    @SuppressWarnings("unchecked")
    public E removeFirst() {
        E x = pollFirst();
        if (x == null) throw new NoSuchElementException();
        return x;
    }

    @Override
    @SuppressWarnings("unchecked")
    public E removeLast() {
        E x = pollLast();
        if (x == null) throw new NoSuchElementException();
        return x;
    }

    @Override
    @SuppressWarnings("unchecked")
    public E pollFirst() {
        int h = head;
        E result = (E) elements[h];
        if (result == null) return null;
        elements[h] = null;
        head = (h + 1) & (elements.length - 1);
        return result;
    }

    @Override
    @SuppressWarnings("unchecked")
    public E pollLast() {
        int t = (tail - 1) & (elements.length - 1);
        E result = (E) elements[t];
        if (result == null) return null;
        elements[t] = null;
        tail = t;
        return result;
    }

    @Override
    @SuppressWarnings("unchecked")
    public E getFirst() {
        E result = (E) elements[head];
        if (result == null) throw new NoSuchElementException();
        return result;
    }

    @Override
    @SuppressWarnings("unchecked")
    public E getLast() {
        E result = (E) elements[(tail - 1) & (elements.length - 1)];
        if (result == null) throw new NoSuchElementException();
        return result;
    }

    @Override
    @SuppressWarnings("unchecked")
    public E peekFirst() {
        return (E) elements[head];
    }

    @Override
    @SuppressWarnings("unchecked")
    public E peekLast() {
        return (E) elements[(tail - 1) & (elements.length - 1)];
    }

    @Override
    public boolean removeFirstOccurrence(Object o) {
        if (o == null) return false;
        int mask = elements.length - 1;
        int i = head;
        Object x;
        while ((x = elements[i]) != null) {
            if (o.equals(x)) {
                delete(i);
                return true;
            }
            i = (i + 1) & mask;
        }
        return false;
    }

    @Override
    public boolean removeLastOccurrence(Object o) {
        if (o == null) return false;
        int mask = elements.length - 1;
        int i = (tail - 1) & mask;
        Object x;
        while ((x = elements[i]) != null) {
            if (o.equals(x)) {
                delete(i);
                return true;
            }
            i = (i - 1) & mask;
        }
        return false;
    }

    private boolean delete(int i) {
        Object[] elements = this.elements;
        int mask = elements.length - 1;
        int h = head;
        int t = tail;
        int front = (i - h) & mask;
        int back = (t - i) & mask;

        if (front >= ((t - h) & mask)) throw new ConcurrentModificationException();

        if (front < back) {
            if (h <= i) {
                System.arraycopy(elements, h, elements, h + 1, front);
            } else {
                System.arraycopy(elements, 0, elements, 1, i);
                elements[0] = elements[mask];
                System.arraycopy(elements, h, elements, h + 1, mask - h);
            }
            elements[h] = null;
            head = (h + 1) & mask;
            return false;
        } else {
            if (i < t) {
                System.arraycopy(elements, i + 1, elements, i, back);
                tail = t - 1;
            } else {
                System.arraycopy(elements, i + 1, elements, i, mask - i);
                elements[mask] = elements[0];
                System.arraycopy(elements, 1, elements, 0, t);
                tail = (t - 1) & mask;
            }
            return true;
        }
    }

    @Override
    public boolean add(E e) {
        addLast(e);
        return true;
    }

    @Override
    public boolean offer(E e) {
        return offerLast(e);
    }

    @Override
    public E remove() {
        return removeFirst();
    }

    @Override
    public E poll() {
        return pollFirst();
    }

    @Override
    public E element() {
        return getFirst();
    }

    @Override
    public E peek() {
        return peekFirst();
    }

    @Override
    public void push(E e) {
        addFirst(e);
    }

    @Override
    public E pop() {
        return removeFirst();
    }

    @Override
    public int size() {
        return (tail - head) & (elements.length - 1);
    }

    @Override
    public boolean isEmpty() {
        return head == tail;
    }

    @Override
    public boolean contains(Object o) {
        if (o == null) return false;
        int mask = elements.length - 1;
        int i = head;
        Object x;
        while ((x = elements[i]) != null) {
            if (o.equals(x)) return true;
            i = (i + 1) & mask;
        }
        return false;
    }

    @Override
    public boolean remove(Object o) {
        return removeFirstOccurrence(o);
    }

    @Override
    public void clear() {
        int h = head;
        int t = tail;
        if (h != t) {
            head = tail = 0;
            int i = h;
            int mask = elements.length - 1;
            do {
                elements[i] = null;
                i = (i + 1) & mask;
            } while (i != t);
        }
    }

    @Override
    public Object[] toArray() {
        return toArray(new Object[size()]);
    }

    @Override
    @SuppressWarnings("unchecked")
    public <T> T[] toArray(T[] a) {
        int size = size();
        if (a.length < size) {
            a = (T[]) java.lang.reflect.Array.newInstance(a.getClass().getComponentType(), size);
        }
        int h = head;
        int t = tail;
        if (h < t) {
            System.arraycopy(elements, h, a, 0, size);
        } else if (h > t) {
            int headPortion = elements.length - h;
            System.arraycopy(elements, h, a, 0, headPortion);
            System.arraycopy(elements, 0, a, headPortion, t);
        }
        if (a.length > size) a[size] = null;
        return a;
    }

    @Override
    public Iterator<E> iterator() {
        return new DeqIterator();
    }

    @Override
    public Iterator<E> descendingIterator() {
        return new DescendingIterator();
    }

    private class DeqIterator implements Iterator<E> {
        private int cursor = head;
        private int fence = tail;
        private int lastRet = -1;

        @Override
        public boolean hasNext() {
            return cursor != fence;
        }

        @Override
        @SuppressWarnings("unchecked")
        public E next() {
            if (cursor == fence) throw new NoSuchElementException();
            E result = (E) elements[cursor];
            lastRet = cursor;
            cursor = (cursor + 1) & (elements.length - 1);
            return result;
        }

        @Override
        public void remove() {
            if (lastRet < 0) throw new IllegalStateException();
            if (delete(lastRet)) {
                cursor = (cursor - 1) & (elements.length - 1);
                fence = tail;
            }
            lastRet = -1;
        }
    }

    private class DescendingIterator implements Iterator<E> {
        private int cursor = tail;
        private int fence = head;
        private int lastRet = -1;

        @Override
        public boolean hasNext() {
            return cursor != fence;
        }

        @Override
        @SuppressWarnings("unchecked")
        public E next() {
            if (cursor == fence) throw new NoSuchElementException();
            cursor = (cursor - 1) & (elements.length - 1);
            E result = (E) elements[cursor];
            lastRet = cursor;
            return result;
        }

        @Override
        public void remove() {
            if (lastRet < 0) throw new IllegalStateException();
            if (!delete(lastRet)) {
                cursor = (cursor + 1) & (elements.length - 1);
                fence = head;
            }
            lastRet = -1;
        }
    }

    @Override
    public boolean containsAll(Collection<?> c) {
        for (Object e : c) {
            if (!contains(e)) return false;
        }
        return true;
    }

    @Override
    public boolean addAll(Collection<? extends E> c) {
        boolean modified = false;
        for (E e : c) {
            if (add(e)) modified = true;
        }
        return modified;
    }

    @Override
    public boolean removeAll(Collection<?> c) {
        boolean modified = false;
        for (Object e : c) {
            while (remove(e)) modified = true;
        }
        return modified;
    }

    @Override
    public boolean retainAll(Collection<?> c) {
        boolean modified = false;
        Iterator<E> it = iterator();
        while (it.hasNext()) {
            if (!c.contains(it.next())) {
                it.remove();
                modified = true;
            }
        }
        return modified;
    }
}
