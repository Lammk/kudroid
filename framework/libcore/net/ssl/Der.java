package libcore.net.ssl;

/** A cursor over DER-encoded data: just enough ASN.1 to read an X.509 certificate. */
final class Der {
    static final int SEQUENCE = 0x30;
    static final int SET = 0x31;
    static final int INTEGER = 0x02;
    static final int BIT_STRING = 0x03;
    static final int OCTET_STRING = 0x04;
    static final int OID = 0x06;
    static final int UTC_TIME = 0x17;
    static final int GENERALIZED_TIME = 0x18;

    final byte[] data;
    int pos;
    final int end;

    // The element most recently read.
    int tag;
    int start;   // first content byte
    int length;  // content length

    Der(byte[] data) {
        this(data, 0, data.length);
    }

    Der(byte[] data, int from, int to) {
        this.data = data;
        this.pos = from;
        this.end = to;
    }

    boolean hasMore() { return pos < end; }

    /** Reads the next header, leaving pos after the whole element. */
    void next() {
        if (pos + 2 > end) throw new IllegalArgumentException("truncated DER");
        tag = data[pos++] & 0xff;
        int l = data[pos++] & 0xff;
        if (l >= 0x80) {
            int n = l & 0x7f;
            if (n == 0 || n > 4 || pos + n > end) throw new IllegalArgumentException("bad DER length");
            l = 0;
            for (int i = 0; i < n; i++) l = (l << 8) | (data[pos++] & 0xff);
        }
        if (l < 0 || pos + l > end) throw new IllegalArgumentException("DER length overruns data");
        start = pos;
        length = l;
        pos = start + l;
    }

    /** Reads the next element, which must have the given tag. */
    void expect(int expected) {
        next();
        if (tag != expected) {
            throw new IllegalArgumentException("expected DER tag 0x" + Integer.toHexString(expected)
                    + " but found 0x" + Integer.toHexString(tag));
        }
    }

    /** A reader over the content of the element just read. */
    Der inside() { return new Der(data, start, start + length); }

    byte[] content() {
        byte[] out = new byte[length];
        System.arraycopy(data, start, out, 0, length);
        return out;
    }

    /** The element just read, header included. */
    byte[] whole(int headerStart) {
        byte[] out = new byte[pos - headerStart];
        System.arraycopy(data, headerStart, out, 0, out.length);
        return out;
    }

    /** The dotted form of the OID just read. */
    String oid() {
        StringBuilder sb = new StringBuilder();
        long value = 0;
        boolean first = true;
        for (int i = start; i < start + length; i++) {
            int b = data[i] & 0xff;
            value = (value << 7) | (b & 0x7f);
            if ((b & 0x80) == 0) {
                if (first) {
                    long a = value < 80 ? value / 40 : 2;
                    sb.append(a).append('.').append(value - a * 40);
                    first = false;
                } else {
                    sb.append('.').append(value);
                }
                value = 0;
            }
        }
        return sb.toString();
    }

    /** The string value of a DirectoryString / IA5String element just read. */
    String string() {
        return new String(data, start, length);
    }
}
