package java.util;

public interface List<E> extends Collection<E> {

    E get(int index);

    E set(int index, E element);

    void add(int index, E element);

    E remove(int index);

    int indexOf(Object o);

    int lastIndexOf(Object o);

    List<E> subList(int fromIndex, int toIndex);

    ListIterator<E> listIterator();

    /** Iterate from {@code index}; implementations that can seek override this. */
    default ListIterator<E> listIterator(int index) {
        if (index < 0 || index > size()) {
            throw new IndexOutOfBoundsException("index: " + index + ", size: " + size());
        }
        ListIterator<E> it = listIterator();
        for (int i = 0; i < index; i++) {
            it.next();
        }
        return it;
    }
}
