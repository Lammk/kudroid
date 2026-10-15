package java.util;

/** Preserve insertion order — HashMap.keySet/entrySet relies on this class. */
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
