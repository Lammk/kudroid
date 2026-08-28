package java.util.concurrent;

import java.util.AbstractQueue;
import java.util.Queue;
import java.util.Collection;
import java.util.Iterator;
import java.io.Serializable;
import java.util.LinkedList;

public class ConcurrentLinkedQueue<E> extends AbstractQueue<E> implements Queue<E>, Serializable {
    private static final long serialVersionUID = 196745693267521676L;
    private final LinkedList<E> list = new LinkedList<E>();

    public ConcurrentLinkedQueue() {}
    public ConcurrentLinkedQueue(Collection<? extends E> c) {
        if (c != null) list.addAll(c);
    }

    public synchronized boolean add(E e) {
        if (e == null) throw new NullPointerException();
        return list.add(e);
    }

    public synchronized boolean offer(E e) {
        return add(e);
    }

    public synchronized E poll() {
        return list.poll();
    }

    public synchronized E peek() {
        return list.peek();
    }

    public synchronized boolean isEmpty() {
        return list.isEmpty();
    }

    public synchronized int size() {
        return list.size();
    }

    public synchronized boolean contains(Object o) {
        return list.contains(o);
    }

    public synchronized boolean remove(Object o) {
        return list.remove(o);
    }

    public synchronized Iterator<E> iterator() {
        return new LinkedList<E>(list).iterator();
    }
}
