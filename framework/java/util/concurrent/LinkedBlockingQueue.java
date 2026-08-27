package java.util.concurrent;

public class LinkedBlockingQueue<E> implements BlockingQueue<E> {

    private final java.util.ArrayList<E> items = new java.util.ArrayList<E>();
    private final int capacity;

    public LinkedBlockingQueue() {
        this(Integer.MAX_VALUE);
    }

    public LinkedBlockingQueue(int capacity) {
        this.capacity = capacity;
    }

    public LinkedBlockingQueue(java.util.Collection<? extends E> c) {
        this(Integer.MAX_VALUE);
        addAll(c);
    }

    public synchronized boolean add(E e) {
        if (!offer(e)) {
            throw new IllegalStateException("queue đầy");
        }
        return true;
    }

    public synchronized boolean offer(E e) {
        if (e == null) {
            throw new NullPointerException();
        }
        if (items.size() >= capacity) {
            return false;
        }
        items.add(e);
        notifyAll();
        return true;
    }

    public synchronized void put(E e) throws InterruptedException {
        while (items.size() >= capacity) {
            wait();
        }
        items.add(e);
        notifyAll();
    }

    public synchronized boolean offer(E e, long timeout, TimeUnit unit)
            throws InterruptedException {
        long deadline = System.currentTimeMillis() + unit.toMillis(timeout);
        while (items.size() >= capacity) {
            long remaining = deadline - System.currentTimeMillis();
            if (remaining <= 0) {
                return false;
            }
            wait(remaining);
        }
        items.add(e);
        notifyAll();
        return true;
    }

    public synchronized E take() throws InterruptedException {
        while (items.isEmpty()) {
            wait();
        }
        E e = items.remove(0);
        notifyAll();
        return e;
    }

    public synchronized E poll() {
        if (items.isEmpty()) {
            return null;
        }
        E e = items.remove(0);
        notifyAll();
        return e;
    }

    public synchronized E poll(long timeout, TimeUnit unit) throws InterruptedException {
        long deadline = System.currentTimeMillis() + unit.toMillis(timeout);
        while (items.isEmpty()) {
            long remaining = deadline - System.currentTimeMillis();
            if (remaining <= 0) {
                return null;
            }
            wait(remaining);
        }
        E e = items.remove(0);
        notifyAll();
        return e;
    }

    public synchronized E peek() {
        return items.isEmpty() ? null : items.get(0);
    }

    public synchronized E remove() {
        if (items.isEmpty()) {
            throw new java.util.NoSuchElementException();
        }
        return items.remove(0);
    }

    public synchronized E element() {
        if (items.isEmpty()) {
            throw new java.util.NoSuchElementException();
        }
        return items.get(0);
    }

    public synchronized int remainingCapacity() {
        return capacity - items.size();
    }

    public synchronized int drainTo(java.util.Collection<? super E> c) {
        int n = items.size();
        for (int i = 0; i < n; i++) {
            c.add(items.get(i));
        }
        items.clear();
        return n;
    }

    public synchronized int size() {
        return items.size();
    }

    public synchronized boolean isEmpty() {
        return items.isEmpty();
    }

    public synchronized boolean contains(Object o) {
        return items.contains(o);
    }

    public synchronized boolean remove(Object o) {
        return items.remove(o);
    }

    public synchronized void clear() {
        items.clear();
    }

    public synchronized boolean containsAll(java.util.Collection<?> c) {
        return items.containsAll(c);
    }

    public synchronized boolean addAll(java.util.Collection<? extends E> c) {
        return items.addAll(c);
    }

    public synchronized boolean removeAll(java.util.Collection<?> c) {
        return items.removeAll(c);
    }

    public synchronized boolean retainAll(java.util.Collection<?> c) {
        return items.retainAll(c);
    }

    public synchronized Object[] toArray() {
        return items.toArray();
    }

    public synchronized <T> T[] toArray(T[] a) {
        return items.toArray(a);
    }

    public java.util.Iterator<E> iterator() {
        return items.iterator();
    }
}
