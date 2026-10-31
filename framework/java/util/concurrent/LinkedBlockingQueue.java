package java.util.concurrent;

public class LinkedBlockingQueue<E> implements BlockingQueue<E> {

    private final java.util.ArrayList<E> items = new java.util.ArrayList<E>();
    private final int capacity;

    public LinkedBlockingQueue() {
        this(Integer.MAX_VALUE);
    }

    public LinkedBlockingQueue(int capacity) {
        if (capacity <= 0) throw new IllegalArgumentException();
        this.capacity = capacity;
    }

    public LinkedBlockingQueue(java.util.Collection<? extends E> c) {
        this(Integer.MAX_VALUE);
        addAll(c);
    }

    public synchronized boolean add(E e) {
        if (!offer(e)) {
            throw new IllegalStateException("queue full");
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
        if (e == null) throw new NullPointerException();
        if (Thread.interrupted()) throw new InterruptedException();
        while (items.size() >= capacity) {
            wait();
        }
        items.add(e);
        notifyAll();
    }

    public synchronized boolean offer(E e, long timeout, TimeUnit unit)
            throws InterruptedException {
        if (e == null) throw new NullPointerException();
        long nanos = unit.toNanos(timeout);
        long start = System.nanoTime();
        if (Thread.interrupted()) throw new InterruptedException();
        while (items.size() >= capacity) {
            long remaining = nanos <= 0 ? 0 : nanos - (System.nanoTime() - start);
            if (remaining <= 0) {
                return false;
            }
            wait(remaining / 1000000L, (int) (remaining % 1000000L));
        }
        items.add(e);
        notifyAll();
        return true;
    }

    public synchronized E take() throws InterruptedException {
        if (Thread.interrupted()) throw new InterruptedException();
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
        long nanos = unit.toNanos(timeout);
        long start = System.nanoTime();
        if (Thread.interrupted()) throw new InterruptedException();
        while (items.isEmpty()) {
            long remaining = nanos <= 0 ? 0 : nanos - (System.nanoTime() - start);
            if (remaining <= 0) {
                return null;
            }
            wait(remaining / 1000000L, (int) (remaining % 1000000L));
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
        return poll();
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
        if (c == null) throw new NullPointerException();
        if (c == this) throw new IllegalArgumentException();
        int n = items.size();
        for (int i = 0; i < n; i++) {
            c.add(items.get(i));
        }
        items.clear();
        notifyAll();
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
        boolean removed = items.remove(o);
        if (removed) notifyAll();
        return removed;
    }

    public synchronized void clear() {
        items.clear();
        notifyAll();
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

    public synchronized java.util.Iterator<E> iterator() {
        final java.util.Iterator<E> snapshot = new java.util.ArrayList<E>(items).iterator();
        return new java.util.Iterator<E>() {
            private E last;
            private boolean removable;
            public boolean hasNext() { return snapshot.hasNext(); }
            public E next() { last = snapshot.next(); removable = true; return last; }
            public void remove() {
                if (!removable) throw new IllegalStateException();
                LinkedBlockingQueue.this.remove(last);
                removable = false;
            }
        };
    }
}
