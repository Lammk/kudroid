package java.util;

public interface Queue<E> extends Collection<E> {

    boolean offer(E e);

    E poll();

    E peek();

    E remove();

    E element();
}
