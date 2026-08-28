package java.io;

public class ObjectStreamField implements Comparable<Object> {
    private final String name;
    private final Class<?> type;

    public ObjectStreamField(String name, Class<?> type) {
        this.name = name;
        this.type = type;
    }

    public String getName() { return name; }
    public Class<?> getType() { return type; }
    public int compareTo(Object obj) {
        ObjectStreamField other = (ObjectStreamField) obj;
        return name.compareTo(other.name);
    }
}
