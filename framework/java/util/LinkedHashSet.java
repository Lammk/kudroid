package java.util;

/** Giữ thứ tự chèn — HashMap.keySet/entrySet dựa vào lớp này. */
public class LinkedHashSet<E> extends HashSet<E> {

    public LinkedHashSet() {
    }

    public LinkedHashSet(int initialCapacity) {
        super(initialCapacity);
    }

    public LinkedHashSet(Collection<? extends E> c) {
        super(c);
    }
}
