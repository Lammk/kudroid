package java.io;

public class ByteArrayOutputStream extends OutputStream {

    protected byte[] buf;
    protected int count;

    public ByteArrayOutputStream() {
        buf = new byte[32];
    }

    public ByteArrayOutputStream(int size) {
        buf = new byte[size < 1 ? 32 : size];
    }

    private void ensure(int minCapacity) {
        if (minCapacity <= buf.length) {
            return;
        }
        int newLen = buf.length * 2 + 2;
        if (newLen < minCapacity) {
            newLen = minCapacity;
        }
        byte[] next = new byte[newLen];
        System.arraycopy(buf, 0, next, 0, count);
        buf = next;
    }

    public void write(int b) {
        ensure(count + 1);
        buf[count++] = (byte) b;
    }

    public void write(byte[] b, int off, int len) {
        ensure(count + len);
        System.arraycopy(b, off, buf, count, len);
        count += len;
    }

    public byte[] toByteArray() {
        byte[] out = new byte[count];
        System.arraycopy(buf, 0, out, 0, count);
        return out;
    }

    public int size() {
        return count;
    }

    public void reset() {
        count = 0;
    }

    public String toString() {
        return new String(buf, 0, count);
    }
}
