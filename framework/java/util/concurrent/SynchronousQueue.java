package java.util.concurrent;

import java.util.AbstractQueue;
import java.util.Collection;
import java.util.Iterator;
import java.util.Collections;
import java.io.Serializable;

public class SynchronousQueue<E> extends AbstractQueue<E> implements BlockingQueue<E>, Serializable {
    private static final long serialVersionUID = -3223113410248163686L;

    // Single handoff slot: put waits for a taker, take waits for a putter.
    private E item = null;
    private boolean hasItem = false;

    public SynchronousQueue() {}
    public SynchronousQueue(boolean fair) {}

    public synchronized void put(E e) throws InterruptedException {
        if (e == null) throw new NullPointerException();
        while (hasItem) wait();
        item = e;
        hasItem = true;
        notifyAll();
        while (hasItem) wait();
    }

    public synchronized boolean offer(E e, long timeout, TimeUnit unit) throws InterruptedException {
        if (e == null) throw new NullPointerException();
        long deadline = System.currentTimeMillis() + unit.toMillis(timeout);
        while (hasItem) {
            long remaining = deadline - System.currentTimeMillis();
            if (remaining <= 0) return false;
            wait(remaining);
        }
        item = e;
        hasItem = true;
        notifyAll();
        return true;
    }

    public synchronized boolean offer(E e) {
        if (e == null) throw new NullPointerException();
        if (hasItem) return false;
        item = e;
        hasItem = true;
        notifyAll();
        return true;
    }

    public synchronized E take() throws InterruptedException {
        while (!hasItem) wait();
        E e = item;
        item = null;
        hasItem = false;
        notifyAll();
        return e;
    }

    public synchronized E poll(long timeout, TimeUnit unit) throws InterruptedException {
        if (!hasItem) {
            long deadline = System.currentTimeMillis() + unit.toMillis(timeout);
            long remaining = deadline - System.currentTimeMillis();
            while (!hasItem && remaining > 0) {
                wait(remaining);
                remaining = deadline - System.currentTimeMillis();
            }
            if (!hasItem) return null;
        }
        E e = item;
        item = null;
        hasItem = false;
        notifyAll();
        return e;
    }

    public E poll() {
        return null;
    }

    public boolean isEmpty() {
        return true;
    }

    public int size() {
        return 0;
    }

    public int remainingCapacity() {
        return 0;
    }

    public void clear() {}

    public boolean contains(Object o) {
        return false;
    }

    public boolean remove(Object o) {
        return false;
    }

    public boolean containsAll(Collection<?> c) {
        return c.isEmpty();
    }

    public boolean removeAll(Collection<?> c) {
        return false;
    }

    public boolean retainAll(Collection<?> c) {
        return false;
    }

    public E peek() {
        return null;
    }

    public Iterator<E> iterator() {
        return Collections.<E>emptyList().iterator();
    }

    public Object[] toArray() {
        return new Object[0];
    }

    public <T> T[] toArray(T[] a) {
        if (a.length > 0)
            a[0] = null;
        return a;
    }

    public int drainTo(Collection<? super E> c) {
        if (c == null) throw new NullPointerException();
        if (c == this) throw new IllegalArgumentException();
        return 0;
    }

    public int drainTo(Collection<? super E> c, int maxElements) {
        if (c == null) throw new NullPointerException();
        if (c == this) throw new IllegalArgumentException();
        return 0;
    }
}
