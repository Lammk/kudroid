package java.lang;

public final class Short extends Number implements Comparable<Short> {

    public static final short MIN_VALUE = -32768;
    public static final short MAX_VALUE = 32767;
    public static final int SIZE = 16;
    public static final int BYTES = 2;
    public static final Class<Short> TYPE = null;

    private final short value;

    public Short(short value) {
        this.value = value;
    }

    public static Short valueOf(short s) {
        return new Short(s);
    }

    public int intValue() {
        return value;
    }

    public long longValue() {
        return value;
    }

    public float floatValue() {
        return value;
    }

    public double doubleValue() {
        return value;
    }

    public byte byteValue() {
        return (byte) value;
    }

    public short shortValue() {
        return value;
    }

    public int hashCode() {
        return value;
    }

    public boolean equals(Object other) {
        return (other instanceof Short) && ((Short) other).value == value;
    }

    public int compareTo(Short other) {
        return value - other.value;
    }

    public String toString() {
        return Integer.toString(value);
    }

    public static String toString(short s) {
        return Integer.toString(s);
    }

    public static int compare(short a, short b) {
        return a - b;
    }

    public static short parseShort(String s) {
        return (short) Integer.parseInt(s);
    }
}
