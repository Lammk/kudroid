package java.lang;

public final class Float extends Number implements Comparable<Float> {

    public static final float MIN_VALUE = 1.4E-45f;
    public static final float MAX_VALUE = 3.4028235E38f;
    public static final float POSITIVE_INFINITY = 1.0f / 0.0f;
    public static final float NEGATIVE_INFINITY = -1.0f / 0.0f;
    public static final float NaN = 0.0f / 0.0f;
    public static final int SIZE = 32;
    public static final int BYTES = 4;
    // float.class compiles to a read of this field, so it must hold the real
    // primitive Class rather than null. See Class.getPrimitiveClass.
    @SuppressWarnings("unchecked")
    public static final Class<Float> TYPE =
            (Class<Float>) Class.getPrimitiveClass("float");

    private final float value;

    public Float(float value) {
        this.value = value;
    }

    public Float(double value) {
        this.value = (float) value;
    }

    public Float(String s) {
        this.value = parseFloat(s);
    }

    public static Float valueOf(float f) {
        return new Float(f);
    }

    public static Float valueOf(String s) {
        return new Float(parseFloat(s));
    }

    public int intValue() {
        return (int) value;
    }

    public long longValue() {
        return (long) value;
    }

    public float floatValue() {
        return value;
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
        return floatToIntBits(value);
    }

    public boolean equals(Object other) {
        return (other instanceof Float)
                && floatToIntBits(((Float) other).value) == floatToIntBits(value);
    }

    public int compareTo(Float other) {
        return compare(value, other.value);
    }

    public String toString() {
        return toString(value);
    }

    public static boolean isNaN(float f) {
        return f != f;
    }

    public static boolean isInfinite(float f) {
        return f == POSITIVE_INFINITY || f == NEGATIVE_INFINITY;
    }

    public static boolean isFinite(float f) {
        return !isNaN(f) && !isInfinite(f);
    }

    public static int compare(float a, float b) {
        if (a < b) {
            return -1;
        }
        if (a > b) {
            return 1;
        }
        int ia = floatToIntBits(a);
        int ib = floatToIntBits(b);
        return ia == ib ? 0 : (ia < ib ? -1 : 1);
    }

    public static float max(float a, float b) {
        return a > b ? a : b;
    }

    public static float min(float a, float b) {
        return a < b ? a : b;
    }

    public static native int floatToIntBits(float f);

    public static native int floatToRawIntBits(float f);

    public static native float intBitsToFloat(int bits);

    public static String toString(float f) {
        return Double.toString(f);
    }

    public static float parseFloat(String s) {
        return (float) Double.parseDouble(s);
    }
}
