package java.lang;

public final class Integer extends Number implements Comparable<Integer> {

    public static final int MIN_VALUE = 0x80000000;
    public static final int MAX_VALUE = 0x7fffffff;
    public static final int SIZE = 32;
    public static final int BYTES = 4;
    public static final Class<Integer> TYPE = null;

    private final int value;

    public Integer(int value) {
        this.value = value;
    }

    public Integer(String s) {
        this.value = parseInt(s);
    }

    public static Integer valueOf(int i) {
        return new Integer(i);
    }

    public static Integer valueOf(String s) {
        return new Integer(parseInt(s));
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
        return (short) value;
    }

    public int hashCode() {
        return value;
    }

    public static int hashCode(int value) {
        return value;
    }

    public boolean equals(Object other) {
        return (other instanceof Integer) && ((Integer) other).value == value;
    }

    public int compareTo(Integer other) {
        return compare(value, other.value);
    }

    public String toString() {
        return toString(value);
    }

    public static int compare(int a, int b) {
        return a < b ? -1 : (a == b ? 0 : 1);
    }

    public static int parseInt(String s) {
        return parseInt(s, 10);
    }

    public static int parseInt(String s, int radix) {
        long v = Long.parseLong(s, radix);
        if (v < MIN_VALUE || v > MAX_VALUE) {
            throw new NumberFormatException("out of int range: " + s);
        }
        return (int) v;
    }

    public static String toString(int i) {
        return Long.toString(i, 10);
    }

    public static String toString(int i, int radix) {
        return Long.toString(i, radix);
    }

    public static String toHexString(int i) {
        return Long.toUnsignedString(i & 0xffffffffL, 16);
    }

    public static String toOctalString(int i) {
        return Long.toUnsignedString(i & 0xffffffffL, 8);
    }

    public static String toBinaryString(int i) {
        return Long.toUnsignedString(i & 0xffffffffL, 2);
    }

    public static int max(int a, int b) {
        return a > b ? a : b;
    }

    public static int min(int a, int b) {
        return a < b ? a : b;
    }

    public static int signum(int i) {
        return i > 0 ? 1 : (i < 0 ? -1 : 0);
    }

    public static int bitCount(int i) {
        int n = 0;
        for (int k = 0; k < 32; k++) {
            if (((i >>> k) & 1) != 0) {
                n++;
            }
        }
        return n;
    }

    public static int reverse(int i) {
        int r = 0;
        for (int k = 0; k < 32; k++) {
            r = (r << 1) | ((i >>> k) & 1);
        }
        return r;
    }

    public static int highestOneBit(int i) {
        for (int k = 31; k >= 0; k--) {
            if (((i >>> k) & 1) != 0) {
                return 1 << k;
            }
        }
        return 0;
    }

    public static int lowestOneBit(int i) {
        return i & -i;
    }

    public static int numberOfLeadingZeros(int i) {
        for (int k = 31; k >= 0; k--) {
            if (((i >>> k) & 1) != 0) {
                return 31 - k;
            }
        }
        return 32;
    }

    public static int numberOfTrailingZeros(int i) {
        for (int k = 0; k < 32; k++) {
            if (((i >>> k) & 1) != 0) {
                return k;
            }
        }
        return 32;
    }

    public static int rotateLeft(int i, int distance) {
        int d = distance & 31;
        return (i << d) | (i >>> (32 - d));
    }

    public static int rotateRight(int i, int distance) {
        int d = distance & 31;
        return (i >>> d) | (i << (32 - d));
    }
}
