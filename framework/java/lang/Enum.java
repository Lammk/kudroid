package java.lang;

/**
 * The parent class of all enums. javac generates values()/valueOf() and the $VALUES array for
 * each child enum so here only need name + ordinal.
 */
public abstract class Enum<E extends Enum<E>> implements Comparable<E> {

    private final String name;
    private final int ordinal;

    protected Enum(String name, int ordinal) {
        this.name = name;
        this.ordinal = ordinal;
    }

    public final String name() {
        return name;
    }

    public final int ordinal() {
        return ordinal;
    }

    public String toString() {
        return name;
    }

    public final boolean equals(Object other) {
        return this == other;
    }

    public final int hashCode() {
        return ordinal;
    }

    public final int compareTo(E other) {
        return ordinal - other.ordinal();
    }

    public final Class<E> getDeclaringClass() {
        return (Class<E>) getClass();
    }

    protected final Object clone() throws CloneNotSupportedException {
        throw new CloneNotSupportedException("enum cannot be cloned");
    }

    public static <T extends Enum<T>> T valueOf(Class<T> enumType, String name) {
        T[] values = enumType.getEnumConstants();
        if (values != null) {
            for (int i = 0; i < values.length; i++) {
                if (values[i].name().equals(name)) {
                    return values[i];
                }
            }
        }
        throw new IllegalArgumentException("no enum constant " + name);
    }
}
