package java.util;

public class LinkedList<E> extends ArrayList<E> implements Queue<E> {

    public LinkedList() {
    }

    public LinkedList(Collection<? extends E> c) {
        super(c);
    }

    public void addFirst(E e) {
        add(0, e);
    }

    public void addLast(E e) {
        add(e);
    }

    public E getFirst() {
        if (isEmpty()) {
            throw new NoSuchElementException();
        }
        return get(0);
    }

    public E getLast() {
        if (isEmpty()) {
            throw new NoSuchElementException();
        }
        return get(size() - 1);
    }

    public E removeFirst() {
        if (isEmpty()) {
            throw new NoSuchElementException();
        }
        return remove(0);
    }

    public E removeLast() {
        if (isEmpty()) {
            throw new NoSuchElementException();
        }
        return remove(size() - 1);
    }

    public boolean offer(E e) {
        return add(e);
    }

    public E poll() {
        return isEmpty() ? null : remove(0);
    }

    public E peek() {
        return isEmpty() ? null : get(0);
    }

    public E remove() {
        return removeFirst();
    }

    public E element() {
        return getFirst();
    }

    public E pop() {
        return removeFirst();
    }

    public void push(E e) {
        addFirst(e);
    }
}
