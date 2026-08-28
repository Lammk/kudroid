package java.util.zip;

public class Adler32 implements Checksum {
    private int adler = 1;

    public void update(int b) {
        int s1 = adler & 0xffff;
        int s2 = (adler >>> 16) & 0xffff;
        s1 = (s1 + (b & 0xff)) % 65521;
        s2 = (s2 + s1) % 65521;
        adler = (s2 << 16) | s1;
    }
    public void update(byte[] b, int off, int len) {
        if (b == null) throw new NullPointerException();
        if (off < 0 || len < 0 || off > b.length - len) throw new ArrayIndexOutOfBoundsException();
        for (int i = 0; i < len; i++) {
            update(b[off + i]);
        }
    }
    public void update(byte[] b) {
        update(b, 0, b.length);
    }
    public void reset() {
        adler = 1;
    }
    public long getValue() {
        return ((long) adler) & 0xffffffffL;
    }
}
