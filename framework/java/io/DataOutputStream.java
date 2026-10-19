package java.io;

public class DataOutputStream extends FilterOutputStream implements DataOutput {
    protected int written;
    private final byte[] work = new byte[8];

    public DataOutputStream(OutputStream out) {
        super(out);
    }

    private void count(int n) {
        final int sum = written + n;
        written = sum < 0 ? Integer.MAX_VALUE : sum;
    }

    public synchronized void write(int b) throws IOException {
        out.write(b);
        count(1);
    }

    public synchronized void write(byte[] b, int off, int len) throws IOException {
        out.write(b, off, len);
        count(len);
    }

    public void flush() throws IOException {
        out.flush();
    }

    public final void writeBoolean(boolean v) throws IOException {
        write(v ? 1 : 0);
    }

    public final void writeByte(int v) throws IOException {
        write(v);
    }

    public final void writeShort(int v) throws IOException {
        work[0] = (byte) (v >>> 8);
        work[1] = (byte) v;
        write(work, 0, 2);
    }

    public final void writeChar(int v) throws IOException {
        writeShort(v);
    }

    public final void writeInt(int v) throws IOException {
        work[0] = (byte) (v >>> 24);
        work[1] = (byte) (v >>> 16);
        work[2] = (byte) (v >>> 8);
        work[3] = (byte) v;
        write(work, 0, 4);
    }

    public final void writeLong(long v) throws IOException {
        for (int i = 0; i < 8; i++) {
            work[i] = (byte) (v >>> (56 - 8 * i));
        }
        write(work, 0, 8);
    }

    public final void writeFloat(float v) throws IOException {
        writeInt(Float.floatToIntBits(v));
    }

    public final void writeDouble(double v) throws IOException {
        writeLong(Double.doubleToLongBits(v));
    }

    public final void writeBytes(String s) throws IOException {
        final int n = s.length();
        final byte[] b = new byte[n];
        for (int i = 0; i < n; i++) {
            b[i] = (byte) s.charAt(i);
        }
        write(b, 0, n);
    }

    public final void writeChars(String s) throws IOException {
        final int n = s.length();
        final byte[] b = new byte[n * 2];
        for (int i = 0; i < n; i++) {
            final char c = s.charAt(i);
            b[2 * i] = (byte) (c >>> 8);
            b[2 * i + 1] = (byte) c;
        }
        write(b, 0, b.length);
    }

    public final void writeUTF(String s) throws IOException {
        final byte[] encoded = encodeUTF(s);
        write(encoded, 0, encoded.length);
    }

    public final int size() {
        return written;
    }

    static int utfLength(String s) throws UTFDataFormatException {
        long length = 0;
        for (int i = 0; i < s.length(); i++) {
            final char c = s.charAt(i);
            length += (c >= 0x0001 && c <= 0x007f) ? 1 : (c <= 0x07ff ? 2 : 3);
        }
        if (length > 65535) {
            throw new UTFDataFormatException("encoded string too long: " + length + " bytes");
        }
        return (int) length;
    }

    /** Modified UTF-8 with a 16-bit length prefix; NUL is encoded as two bytes. */
    static byte[] encodeUTF(String s) throws IOException {
        final int length = utfLength(s);
        final byte[] out = new byte[length + 2];
        out[0] = (byte) (length >>> 8);
        out[1] = (byte) length;
        int p = 2;
        for (int i = 0; i < s.length(); i++) {
            final char c = s.charAt(i);
            if (c >= 0x0001 && c <= 0x007f) {
                out[p++] = (byte) c;
            } else if (c <= 0x07ff) {
                out[p++] = (byte) (0xc0 | (c >> 6));
                out[p++] = (byte) (0x80 | (c & 0x3f));
            } else {
                out[p++] = (byte) (0xe0 | (c >> 12));
                out[p++] = (byte) (0x80 | ((c >> 6) & 0x3f));
                out[p++] = (byte) (0x80 | (c & 0x3f));
            }
        }
        return out;
    }
}
