package java.util;

public class UUID {

    private final long most;
    private final long least;

    public UUID(long mostSigBits, long leastSigBits) {
        this.most = mostSigBits;
        this.least = leastSigBits;
    }

    public long getMostSignificantBits() {
        return most;
    }

    public long getLeastSignificantBits() {
        return least;
    }

    public static UUID randomUUID() {
        Random r = new Random();
        long hi = r.nextLong();
        long lo = r.nextLong();
        // Đặt version 4 và variant IETF như spec.
        hi = (hi & ~0xf000L) | 0x4000L;
        lo = (lo & 0x3fffffffffffffffL) | 0x8000000000000000L;
        return new UUID(hi, lo);
    }

    public boolean equals(Object other) {
        if (!(other instanceof UUID)) {
            return false;
        }
        UUID u = (UUID) other;
        return u.most == most && u.least == least;
    }

    public int hashCode() {
        long x = most ^ least;
        return (int) (x ^ (x >>> 32));
    }

    public String toString() {
        return hex(most >>> 32, 8) + "-" + hex(most >>> 16, 4) + "-" + hex(most, 4) + "-"
                + hex(least >>> 48, 4) + "-" + hex(least, 12);
    }

    private static String hex(long value, int digits) {
        String s = Long.toHexString(value & ((1L << (digits * 4)) - 1));
        StringBuilder sb = new StringBuilder();
        for (int i = s.length(); i < digits; i++) {
            sb.append('0');
        }
        return sb.append(s).toString();
    }
}
