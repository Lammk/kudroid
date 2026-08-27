package java.lang;

public final class Boolean implements Comparable<Boolean> {

    public static final Boolean TRUE = new Boolean(true);
    public static final Boolean FALSE = new Boolean(false);
    public static final Class<Boolean> TYPE = null;

    private final boolean value;

    public Boolean(boolean value) {
        this.value = value;
    }

    public Boolean(String s) {
        this.value = parseBoolean(s);
    }

    public static Boolean valueOf(boolean b) {
        return b ? TRUE : FALSE;
    }

    public static Boolean valueOf(String s) {
        return valueOf(parseBoolean(s));
    }

    public boolean booleanValue() {
        return value;
    }

    public int hashCode() {
        return value ? 1231 : 1237;
    }

    public boolean equals(Object other) {
        return (other instanceof Boolean) && ((Boolean) other).value == value;
    }

    public int compareTo(Boolean other) {
        return compare(value, other.value);
    }

    public String toString() {
        return toString(value);
    }

    public static int compare(boolean a, boolean b) {
        return a == b ? 0 : (a ? 1 : -1);
    }

    public static String toString(boolean b) {
        return b ? "true" : "false";
    }

    public static boolean parseBoolean(String s) {
        return s != null && s.equalsIgnoreCase("true");
    }
}
