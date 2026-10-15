package java.lang;

public final class Double extends Number implements Comparable<Double> {

    public static final double MIN_VALUE = 4.9E-324;
    public static final double MAX_VALUE = 1.7976931348623157E308;
    public static final double POSITIVE_INFINITY = 1.0 / 0.0;
    public static final double NEGATIVE_INFINITY = -1.0 / 0.0;
    public static final double NaN = 0.0 / 0.0;
    public static final int SIZE = 64;
    public static final int BYTES = 8;
    // double.class compiles to a read of this field, so it must hold the real
    // primitive Class rather than null. See Class.getPrimitiveClass.
    @SuppressWarnings("unchecked")
    public static final Class<Double> TYPE =
            (Class<Double>) Class.getPrimitiveClass("double");

    private final double value;

    public Double(double value) {
        this.value = value;
    }

    public Double(String s) {
        this.value = parseDouble(s);
    }

    public static Double valueOf(double d) {
        return new Double(d);
    }

    public static Double valueOf(String s) {
        return new Double(parseDouble(s));
    }

    public int intValue() {
        return (int) value;
    }

    public long longValue() {
        return (long) value;
    }

    public float floatValue() {
        return (float) value;
    }

    public double doubleValue() {
        return value;
    }

    public boolean isNaN() {
        return isNaN(value);
    }

    public boolean isInfinite() {
        return isInfinite(value);
    }

    public int hashCode() {
        return hashCode(value);
    }

    public static int hashCode(double value) {
        long bits = doubleToLongBits(value);
        return (int) (bits ^ (bits >>> 32));
    }

    public boolean equals(Object other) {
        return (other instanceof Double)
                && doubleToLongBits(((Double) other).value) == doubleToLongBits(value);
    }

    public int compareTo(Double other) {
        return compare(value, other.value);
    }

    public String toString() {
        return toString(value);
    }

    public static boolean isNaN(double d) {
        return d != d;
    }

    public static boolean isInfinite(double d) {
        return d == POSITIVE_INFINITY || d == NEGATIVE_INFINITY;
    }

    public static boolean isFinite(double d) {
        return !isNaN(d) && !isInfinite(d);
    }

    public static int compare(double a, double b) {
        if (a < b) {
            return -1;
        }
        if (a > b) {
            return 1;
        }
        long la = doubleToLongBits(a);
        long lb = doubleToLongBits(b);
        return la == lb ? 0 : (la < lb ? -1 : 1);
    }

    public static double max(double a, double b) {
        return a > b ? a : b;
    }

    public static double min(double a, double b) {
        return a < b ? a : b;
    }

    public static native long doubleToLongBits(double d);

    public static native long doubleToRawLongBits(double d);

    public static native double longBitsToDouble(long bits);

    /** Format using C snprintf — don't write Ryu/Grisu yourself. */
    public static native String toString(double d);

    /** strtod of C. */
    public static native double parseDouble(String s);
}
