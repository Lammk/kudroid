package java.util.zip;

import libcore.io.KuZlib;

public class Adler32 implements Checksum {
    private int adler = 1;

    public void update(int b) {
        byte[] one = {(byte) b};
        adler = KuZlib.adler32(adler, one, 0, 1);
    }

    public void update(byte[] b, int off, int len) {
        if (b == null) throw new NullPointerException();
        if (off < 0 || len < 0 || off > b.length - len) throw new ArrayIndexOutOfBoundsException();
        adler = KuZlib.adler32(adler, b, off, len);
    }

    public void update(byte[] b) { update(b, 0, b.length); }
    public void reset() { adler = 1; }
    public long getValue() { return ((long) adler) & 0xffffffffL; }
}
