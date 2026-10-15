package java.util.concurrent;

public interface BlockingQueue<E> extends java.util.Queue<E> {

    void put(E e) throws InterruptedException;

    E take() throws InterruptedException;

    E poll(long timeout, TimeUnit unit) throws InterruptedException;

    boolean offer(E e, long timeout, TimeUnit unit) throws InterruptedException;

    int remainingCapacity();

    int drainTo(java.util.Collection<? super E> c);
}
