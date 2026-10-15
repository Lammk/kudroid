package java.io;

public class ByteArrayInputStream extends InputStream {

    protected byte[] buf;
    protected int pos;
    protected int count;
    protected int mark;

    public ByteArrayInputStream(byte[] buf) {
        this(buf, 0, buf.length);
    }

    public ByteArrayInputStream(byte[] buf, int offset, int length) {
        this.buf = buf;
        this.pos = offset;
        this.count = Math.min(offset + length, buf.length);
        this.mark = offset;
    }

    public int read() {
        return pos < count ? (buf[pos++] & 0xff) : -1;
    }

    public int read(byte[] b, int off, int len) {
        if (pos >= count) {
            return -1;
        }
        int n = Math.min(len, count - pos);
        System.arraycopy(buf, pos, b, off, n);
        pos += n;
        return n;
    }

    public long skip(long n) {
        long actual = Math.min(n, count - pos);
        if (actual < 0) {
            actual = 0;
        }
        pos += (int) actual;
        return actual;
    }

    public int available() {
        return count - pos;
    }

    public void mark(int readlimit) {
        mark = pos;
    }

    public void reset() {
        pos = mark;
    }

    public boolean markSupported() {
        return true;
    }
}
