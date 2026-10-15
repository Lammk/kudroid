package java.lang;

public final class Byte extends Number implements Comparable<Byte> {

    public static final byte MIN_VALUE = -128;
    public static final byte MAX_VALUE = 127;
    public static final int SIZE = 8;
    public static final int BYTES = 1;
    // byte.class compiles to a read of this field, so it must hold the real
    // primitive Class rather than null. See Class.getPrimitiveClass.
    @SuppressWarnings("unchecked")
    public static final Class<Byte> TYPE =
            (Class<Byte>) Class.getPrimitiveClass("byte");

    private final byte value;

    public Byte(byte value) {
        this.value = value;
    }

    public static Byte valueOf(byte b) {
        return new Byte(b);
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
        return value;
    }

    public short shortValue() {
        return value;
    }

    public int hashCode() {
        return value;
    }

    public boolean equals(Object other) {
        return (other instanceof Byte) && ((Byte) other).value == value;
    }

    public int compareTo(Byte other) {
        return value - other.value;
    }

    public String toString() {
        return Integer.toString(value);
    }

    public static String toString(byte b) {
        return Integer.toString(b);
    }

    public static int compare(byte a, byte b) {
        return a - b;
    }

    public static byte parseByte(String s) {
        return (byte) Integer.parseInt(s);
    }
}
