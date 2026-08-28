package java.util.zip;

public class CRC32 implements Checksum {
    private int crc = 0;

    public void update(int b) {
        crc = updateByte(crc, b);
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
        crc = 0;
    }
    public long getValue() {
        return ((long) crc) & 0xffffffffL;
    }

    private static int updateByte(int crc, int b) {
        int c = (crc ^ b) & 0xff;
        for (int i = 0; i < 8; i++) {
            if ((c & 1) != 0) c = 0xedb88320 ^ (c >>> 1);
            else c = c >>> 1;
        }
        return (crc >>> 8) ^ c;
    }
}
