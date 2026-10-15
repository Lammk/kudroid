package java.util;

import java.io.Serializable;

public abstract class EnumSet<E extends Enum<E>> extends AbstractSet<E> implements Cloneable, Serializable {
    private static final long serialVersionUID = 1009978538140807360L;
    final Class<E> elementType;
    final Enum<?>[] universe;

    EnumSet(Class<E> elementType, Enum<?>[] universe) {
        this.elementType = elementType;
        this.universe = universe;
    }

    public static <E extends Enum<E>> EnumSet<E> noneOf(Class<E> elementType) {
        return new RegularEnumSet<E>(elementType);
    }
    public static <E extends Enum<E>> EnumSet<E> allOf(Class<E> elementType) {
        EnumSet<E> result = noneOf(elementType);
        result.addAll();
        return result;
    }
    abstract void addAll();
    public static <E extends Enum<E>> EnumSet<E> of(E e) {
        EnumSet<E> result = noneOf(e.getDeclaringClass());
        result.add(e);
        return result;
    }
    public static <E extends Enum<E>> EnumSet<E> of(E e1, E e2) {
        EnumSet<E> result = noneOf(e1.getDeclaringClass());
        result.add(e1);
        result.add(e2);
        return result;
    }
    public static <E extends Enum<E>> EnumSet<E> of(E e1, E e2, E e3) {
        EnumSet<E> result = noneOf(e1.getDeclaringClass());
        result.add(e1);
        result.add(e2);
        result.add(e3);
        return result;
    }
    public static <E extends Enum<E>> EnumSet<E> copyOf(EnumSet<E> s) {
        return s.clone();
    }
    public static <E extends Enum<E>> EnumSet<E> copyOf(Collection<E> c) {
        if (c instanceof EnumSet) return ((EnumSet<E>)c).clone();
        if (c.isEmpty()) throw new IllegalArgumentException("Collection is empty");
        Iterator<E> i = c.iterator();
        E first = i.next();
        EnumSet<E> result = EnumSet.of(first);
        while (i.hasNext()) result.add(i.next());
        return result;
    }
    public EnumSet<E> clone() {
        EnumSet<E> result = noneOf(elementType);
        result.addAll(this);
        return result;
    }
}

class RegularEnumSet<E extends Enum<E>> extends EnumSet<E> {
    private final List<E> list = new ArrayList<E>();

    RegularEnumSet(Class<E> elementType) {
        super(elementType, elementType.getEnumConstants());
    }
    void addAll() {
        list.clear();
        for (Enum<?> e : universe) {
            list.add((E) e);
        }
    }
    public Iterator<E> iterator() { return list.iterator(); }
    public int size() { return list.size(); }
    public boolean isEmpty() { return list.isEmpty(); }
    public boolean contains(Object e) { return list.contains(e); }
    public boolean add(E e) {
        if (e == null) throw new NullPointerException();
        if (!list.contains(e)) {
            list.add(e);
            return true;
        }
        return false;
    }
    public boolean remove(Object e) { return list.remove(e); }
    public void clear() { list.clear(); }
}
