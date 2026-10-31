package java.util.concurrent;

import java.util.AbstractQueue;
import java.util.ArrayList;
import java.util.Collection;
import java.util.Iterator;
import java.util.NoSuchElementException;
import java.util.PriorityQueue;

/** A monitor-protected queue that releases elements only after their delay expires. */
public class DelayQueue<E extends Delayed> extends AbstractQueue<E> implements BlockingQueue<E> {
    private final PriorityQueue<E> items = new PriorityQueue<E>();

    public DelayQueue() {}
    public DelayQueue(Collection<? extends E> c) { addAll(c); }

    public synchronized boolean offer(E e) {
        if (e == null) throw new NullPointerException();
        items.offer(e);
        notifyAll();
        return true;
    }
    public void put(E e) { offer(e); }
    public boolean offer(E e, long timeout, TimeUnit unit) { return offer(e); }
    public synchronized E peek() { return items.peek(); }
    public synchronized E poll() {
        E first = items.peek();
        if (first == null || first.getDelay(TimeUnit.NANOSECONDS) > 0) return null;
        return items.poll();
    }
    public synchronized E take() throws InterruptedException {
        while (true) {
            if (Thread.interrupted()) throw new InterruptedException();
            E first = items.peek();
            if (first == null) wait();
            else {
                long delay = first.getDelay(TimeUnit.NANOSECONDS);
                if (delay <= 0) return items.poll();
                wait(delay / 1000000L, (int) (delay % 1000000L));
            }
        }
    }
    public synchronized E poll(long timeout, TimeUnit unit) throws InterruptedException {
        long nanos = unit.toNanos(timeout);
        long start = System.nanoTime();
        while (true) {
            if (Thread.interrupted()) throw new InterruptedException();
            E first = items.peek();
            long delay = first == null ? Long.MAX_VALUE : first.getDelay(TimeUnit.NANOSECONDS);
            if (delay <= 0) return items.poll();
            long remaining = nanos <= 0 ? 0 : nanos - (System.nanoTime() - start);
            if (remaining <= 0) return null;
            long pause = Math.min(delay, remaining);
            wait(pause / 1000000L, (int) (pause % 1000000L));
        }
    }
    public synchronized int size() { return items.size(); }
    public int remainingCapacity() { return Integer.MAX_VALUE; }
    public synchronized boolean contains(Object o) { return items.contains(o); }
    public synchronized boolean remove(Object o) {
        boolean removed = items.remove(o);
        if (removed) notifyAll();
        return removed;
    }
    public synchronized void clear() { items.clear(); notifyAll(); }
    public int drainTo(Collection<? super E> c) { return drainTo(c, Integer.MAX_VALUE); }
    public synchronized int drainTo(Collection<? super E> c, int maximum) {
        if (c == null) throw new NullPointerException();
        if (c == this) throw new IllegalArgumentException();
        int n = 0;
        while (n < maximum) {
            E first = items.peek();
            if (first == null || first.getDelay(TimeUnit.NANOSECONDS) > 0) break;
            c.add(first);
            items.poll();
            n++;
        }
        return n;
    }
    public synchronized Iterator<E> iterator() {
        final Iterator<E> snapshot = new ArrayList<E>(items).iterator();
        return new Iterator<E>() {
            E last;
            boolean removable;
            public boolean hasNext() { return snapshot.hasNext(); }
            public E next() { last = snapshot.next(); removable = true; return last; }
            public void remove() {
                if (!removable) throw new IllegalStateException();
                DelayQueue.this.remove(last);
                removable = false;
            }
        };
    }
}
