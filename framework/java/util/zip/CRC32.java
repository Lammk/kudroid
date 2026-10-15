package java.util.zip;

import libcore.io.KuZlib;

public class CRC32 implements Checksum {
    private int crc = 0;

    public void update(int b) {
        byte[] one = {(byte) b};
        crc = KuZlib.crc32(crc, one, 0, 1);
    }

    public void update(byte[] b, int off, int len) {
        if (b == null) throw new NullPointerException();
        if (off < 0 || len < 0 || off > b.length - len) throw new ArrayIndexOutOfBoundsException();
        crc = KuZlib.crc32(crc, b, off, len);
    }

    public void update(byte[] b) { update(b, 0, b.length); }
    public void reset() { crc = 0; }
    public long getValue() { return ((long) crc) & 0xffffffffL; }
}
