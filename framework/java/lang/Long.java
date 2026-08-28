package java.lang;

public final class Long extends Number implements Comparable<Long> {

    public static final long MIN_VALUE = 0x8000000000000000L;
    public static final long MAX_VALUE = 0x7fffffffffffffffL;
    public static final int SIZE = 64;
    public static final int BYTES = 8;
    public static final Class<Long> TYPE = null;

    private final long value;

    public Long(long value) {
        this.value = value;
    }

    public Long(String s) {
        this.value = parseLong(s);
    }

    public static Long valueOf(long l) {
        return new Long(l);
    }

    public static Long valueOf(String s) {
        return new Long(parseLong(s));
    }

    public int intValue() {
        return (int) value;
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

    public int hashCode() {
        return hashCode(value);
    }

    public static int hashCode(long value) {
        return (int) (value ^ (value >>> 32));
    }

    public boolean equals(Object other) {
        return (other instanceof Long) && ((Long) other).value == value;
    }

    public int compareTo(Long other) {
        return compare(value, other.value);
    }

    public String toString() {
        return toString(value);
    }

    public static int compare(long a, long b) {
        return a < b ? -1 : (a == b ? 0 : 1);
    }

    public static long max(long a, long b) {
        return a > b ? a : b;
    }

    public static long min(long a, long b) {
        return a < b ? a : b;
    }

    public static int signum(long l) {
        return l > 0 ? 1 : (l < 0 ? -1 : 0);
    }

    public static long parseLong(String s) {
        return parseLong(s, 10);
    }

    public static long parseLong(String s, int radix) {
        if (s == null) {
            throw new NumberFormatException("null");
        }
        int len = s.length();
        if (len == 0) {
            throw new NumberFormatException("empty string");
        }
        if (radix < 2 || radix > 36) {
            throw new NumberFormatException("radix " + radix);
        }
        int i = 0;
        boolean negative = false;
        char first = s.charAt(0);
        if (first == '-') {
            negative = true;
            i = 1;
        } else if (first == '+') {
            i = 1;
        }
        if (i == len) {
            throw new NumberFormatException(s);
        }
        // Accumulate on the negative side so that MIN_VALUE can be represented (the positive side is missing 1 value).
        long result = 0;
        long limit = negative ? MIN_VALUE : -MAX_VALUE;
        long multMin = limit / radix;
        while (i < len) {
            int digit = Character.digit(s.charAt(i), radix);
            if (digit < 0) {
                throw new NumberFormatException(s);
            }
            if (result < multMin) {
                throw new NumberFormatException(s);
            }
            result *= radix;
            if (result < limit + digit) {
                throw new NumberFormatException(s);
            }
            result -= digit;
            i++;
        }
        return negative ? result : -result;
    }

    public static String toString(long l) {
        return toString(l, 10);
    }

    public static String toString(long l, int radix) {
        if (radix < 2 || radix > 36) {
            radix = 10;
        }
        if (l == 0) {
            return "0";
        }
        boolean negative = l < 0;
        char[] buf = new char[65];
        int pos = buf.length;
        // Divide on the negative side so that MIN_VALUE does not overflow when comparing.
        long v = negative ? l : -l;
        while (v != 0) {
            int digit = (int) -(v % radix);
            buf[--pos] = Character.forDigit(digit, radix);
            v = v / radix;
        }
        if (negative) {
            buf[--pos] = '-';
        }
        return new String(buf, pos, buf.length - pos);
    }

    public static String toUnsignedString(long l, int radix) {
        if (l == 0) {
            return "0";
        }
        if (l > 0) {
            return toString(l, radix);
        }
        // Bit 63 on: pre-divide once using homemade unsigned division.
        long quotient = (l >>> 1) / radix * 2;
        long remainder = l - quotient * radix;
        while (remainder >= radix) {
            remainder -= radix;
            quotient++;
        }
        return toString(quotient, radix) + Character.forDigit((int) remainder, radix);
    }

    public static String toUnsignedString(long l) {
        return toUnsignedString(l, 10);
    }

    public static String toHexString(long l) {
        return toUnsignedString(l, 16);
    }

    public static String toOctalString(long l) {
        return toUnsignedString(l, 8);
    }

    public static String toBinaryString(long l) {
        return toUnsignedString(l, 2);
    }

    public static int bitCount(long l) {
        int n = 0;
        for (int k = 0; k < 64; k++) {
            if (((l >>> k) & 1L) != 0) {
                n++;
            }
        }
        return n;
    }

    public static int numberOfLeadingZeros(long l) {
        for (int k = 63; k >= 0; k--) {
            if (((l >>> k) & 1L) != 0) {
                return 63 - k;
            }
        }
        return 64;
    }

    public static int numberOfTrailingZeros(long l) {
        for (int k = 0; k < 64; k++) {
            if (((l >>> k) & 1L) != 0) {
                return k;
            }
        }
        return 64;
    }

    public static long highestOneBit(long l) {
        for (int k = 63; k >= 0; k--) {
            if (((l >>> k) & 1L) != 0) {
                return 1L << k;
            }
        }
        return 0;
    }

    public static long lowestOneBit(long l) {
        return l & -l;
    }

    public static long rotateLeft(long l, int distance) {
        int d = distance & 63;
        return (l << d) | (l >>> (64 - d));
    }

    public static long rotateRight(long l, int distance) {
        int d = distance & 63;
        return (l >>> d) | (l << (64 - d));
    }
}
