package java.util.concurrent;

import java.util.AbstractQueue;
import java.util.Collection;
import java.util.Iterator;
import java.util.NoSuchElementException;
import java.io.Serializable;

public class ArrayBlockingQueue<E> extends AbstractQueue<E> implements BlockingQueue<E>, Serializable {
    private static final long serialVersionUID = -817911632652898426L;
    private final Object[] items;
    private int takeIndex;
    private int putIndex;
    private int count;

    public ArrayBlockingQueue(int capacity) {
        this(capacity, false);
    }

    public ArrayBlockingQueue(int capacity, boolean fair) {
        if (capacity <= 0) throw new IllegalArgumentException();
        this.items = new Object[capacity];
    }

    public ArrayBlockingQueue(int capacity, boolean fair, Collection<? extends E> c) {
        this(capacity, fair);
        for (E e : c) add(e);
    }

    public synchronized boolean offer(E e) {
        if (e == null) throw new NullPointerException();
        if (count == items.length) return false;
        items[putIndex] = e;
        if (++putIndex == items.length) putIndex = 0;
        count++;
        notifyAll();
        return true;
    }

    public synchronized void put(E e) throws InterruptedException {
        if (e == null) throw new NullPointerException();
        while (count == items.length) {
            wait();
        }
        items[putIndex] = e;
        if (++putIndex == items.length) putIndex = 0;
        count++;
        notifyAll();
    }

    public synchronized boolean offer(E e, long timeout, TimeUnit unit) throws InterruptedException {
        if (e == null) throw new NullPointerException();
        return offer(e);
    }

    @SuppressWarnings("unchecked")
    public synchronized E poll() {
        if (count == 0) return null;
        E x = (E) items[takeIndex];
        items[takeIndex] = null;
        if (++takeIndex == items.length) takeIndex = 0;
        count--;
        notifyAll();
        return x;
    }

    public synchronized E take() throws InterruptedException {
        while (count == 0) {
            wait();
        }
        return poll();
    }

    public synchronized E poll(long timeout, TimeUnit unit) throws InterruptedException {
        return poll();
    }

    @SuppressWarnings("unchecked")
    public synchronized E peek() {
        return (count == 0) ? null : (E) items[takeIndex];
    }

    public synchronized int size() {
        return count;
    }

    public synchronized int remainingCapacity() {
        return items.length - count;
    }

    public synchronized int drainTo(Collection<? super E> c) {
        return drainTo(c, Integer.MAX_VALUE);
    }

    public synchronized int drainTo(Collection<? super E> c, int maxElements) {
        if (c == null) throw new NullPointerException();
        if (c == this) throw new IllegalArgumentException();
        int n = 0;
        while (n < maxElements && count > 0) {
            c.add(poll());
            n++;
        }
        return n;
    }

    public synchronized Iterator<E> iterator() {
        final Object[] snapshot = toArray();
        return new Iterator<E>() {
            private int cursor = 0;
            public boolean hasNext() { return cursor < snapshot.length; }
            @SuppressWarnings("unchecked")
            public E next() {
                if (!hasNext()) throw new NoSuchElementException();
                return (E) snapshot[cursor++];
            }
            public void remove() { throw new UnsupportedOperationException(); }
        };
    }
}
