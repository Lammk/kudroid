package java.util;

import java.io.Serializable;

public final class UUID implements Serializable, Comparable<UUID> {
    private static final long serialVersionUID = -4856846379112484227L;

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

    public int version() {
        return (int) ((most >> 12) & 0x0f);
    }

    public int variant() {
        return (int) ((least >>> (64 - (least >>> 62))) & (least >> 63));
    }

    public static UUID randomUUID() {
        Random r = new Random();
        long hi = r.nextLong();
        long lo = r.nextLong();
        hi = (hi & ~0xf000L) | 0x4000L;
        lo = (lo & 0x3fffffffffffffffL) | 0x8000000000000000L;
        return new UUID(hi, lo);
    }

    public static UUID nameUUIDFromBytes(byte[] name) {
        if (name == null) {
            throw new NullPointerException("name == null");
        }
        byte[] md5Bytes = md5(name);
        long msb = 0;
        long lsb = 0;
        for (int i = 0; i < 8; i++) {
            msb = (msb << 8) | (md5Bytes[i] & 0xff);
        }
        for (int i = 8; i < 16; i++) {
            lsb = (lsb << 8) | (md5Bytes[i] & 0xff);
        }
        msb &= ~0x000000000000f000L;
        msb |= 0x0000000000003000L;
        lsb &= ~0xc000000000000000L;
        lsb |= 0x8000000000000000L;
        return new UUID(msb, lsb);
    }

    public static UUID fromString(String name) {
        if (name == null) {
            throw new NullPointerException("name == null");
        }
        String[] components = name.split("-");
        if (components.length != 5) {
            throw new IllegalArgumentException("Invalid UUID string: " + name);
        }
        long mostSigBits = Long.parseLong(components[0], 16);
        mostSigBits <<= 16;
        mostSigBits |= Long.parseLong(components[1], 16);
        mostSigBits <<= 16;
        mostSigBits |= Long.parseLong(components[2], 16);

        long leastSigBits = Long.parseLong(components[3], 16);
        leastSigBits <<= 48;
        leastSigBits |= Long.parseLong(components[4], 16);

        return new UUID(mostSigBits, leastSigBits);
    }

    @Override
    public boolean equals(Object other) {
        if (!(other instanceof UUID)) {
            return false;
        }
        UUID u = (UUID) other;
        return u.most == most && u.least == least;
    }

    @Override
    public int hashCode() {
        long x = most ^ least;
        return (int) (x ^ (x >>> 32));
    }

    @Override
    public int compareTo(UUID val) {
        if (this.most < val.most) return -1;
        if (this.most > val.most) return 1;
        if (this.least < val.least) return -1;
        if (this.least > val.least) return 1;
        return 0;
    }

    @Override
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

    private static byte[] md5(byte[] data) {
        int initA = 0x67452301;
        int initB = (int) 0xefcdab89L;
        int initC = (int) 0x98badcfeL;
        int initD = 0x10325476;

        int[] shift1 = {7, 12, 17, 22};
        int[] shift2 = {5, 9, 14, 20};
        int[] shift3 = {4, 11, 16, 23};
        int[] shift4 = {6, 10, 15, 21};

        int[] tableT = new int[64];
        for (int i = 0; i < 64; i++) {
            tableT[i] = (int) ((long) ((1L << 32) * Math.abs(Math.sin(i + 1))));
        }

        int numBlocks = ((data.length + 8) >>> 6) + 1;
        int totalLen = numBlocks << 6;
        byte[] padding = new byte[totalLen - data.length];
        padding[0] = (byte) 0x80;
        long messageLenBits = ((long) data.length) << 3;
        for (int i = 0; i < 8; i++) {
            padding[padding.length - 8 + i] = (byte) (messageLenBits >>> (8 * i));
        }

        int a = initA, b = initB, c = initC, d = initD;
        int[] words = new int[16];

        for (int i = 0; i < numBlocks; i++) {
            int blockOffset = i << 6;
            for (int j = 0; j < 16; j++) {
                int byteOffset = blockOffset + (j << 2);
                int b0 = (byteOffset < data.length) ? (data[byteOffset] & 0xff) : (padding[byteOffset - data.length] & 0xff);
                int b1 = (byteOffset + 1 < data.length) ? (data[byteOffset + 1] & 0xff) : (padding[byteOffset + 1 - data.length] & 0xff);
                int b2 = (byteOffset + 2 < data.length) ? (data[byteOffset + 2] & 0xff) : (padding[byteOffset + 2 - data.length] & 0xff);
                int b3 = (byteOffset + 3 < data.length) ? (data[byteOffset + 3] & 0xff) : (padding[byteOffset + 3 - data.length] & 0xff);
                words[j] = b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
            }

            int originalA = a, originalB = b, originalC = c, originalD = d;

            for (int j = 0; j < 64; j++) {
                int div16 = j >>> 4;
                int f = 0, bufferIndex = j;
                switch (div16) {
                    case 0:
                        f = (b & c) | (~b & d);
                        break;
                    case 1:
                        f = (b & d) | (c & ~d);
                        bufferIndex = (bufferIndex * 5 + 1) & 0x0f;
                        break;
                    case 2:
                        f = b ^ c ^ d;
                        bufferIndex = (bufferIndex * 3 + 5) & 0x0f;
                        break;
                    case 3:
                        f = c ^ (b | ~d);
                        bufferIndex = (bufferIndex * 7) & 0x0f;
                        break;
                }
                int temp = b + Integer.rotateLeft(a + f + words[bufferIndex] + tableT[j],
                        div16 == 0 ? shift1[j & 3] : div16 == 1 ? shift2[j & 3] : div16 == 2 ? shift3[j & 3] : shift4[j & 3]);
                a = d;
                d = c;
                c = b;
                b = temp;
            }

            a += originalA;
            b += originalB;
            c += originalC;
            d += originalD;
        }

        byte[] result = new byte[16];
        for (int i = 0; i < 4; i++) {
            result[i] = (byte) (a >>> (8 * i));
            result[i + 4] = (byte) (b >>> (8 * i));
            result[i + 8] = (byte) (c >>> (8 * i));
            result[i + 12] = (byte) (d >>> (8 * i));
        }
        return result;
    }
}
