package java.util.zip;

import libcore.io.KuZlib;

public class Deflater {
    public static final int DEFLATED = 8;
    public static final int NO_COMPRESSION = 0;
    public static final int BEST_SPEED = 1;
    public static final int BEST_COMPRESSION = 9;
    public static final int DEFAULT_COMPRESSION = -1;
    public static final int FILTERED = 1;
    public static final int HUFFMAN_ONLY = 2;
    public static final int DEFAULT_STRATEGY = 0;
    public static final int NO_FLUSH = 0;
    public static final int SYNC_FLUSH = 2;
    public static final int FULL_FLUSH = 3;

    private static final byte[] EMPTY = new byte[0];

    private long handle;
    private byte[] buf = EMPTY;
    private int off;
    private int len;
    private boolean finish;
    private boolean finished;
    private long totalIn;
    private long totalOut;
    private final int[] counts = new int[2];

    public Deflater(int level, boolean nowrap) {
        if (level < -1 || level > 9) throw new IllegalArgumentException("Bad level: " + level);
        handle = KuZlib.deflateInit(level, nowrap, DEFAULT_STRATEGY);
        if (handle == 0) throw new OutOfMemoryError("could not allocate a deflater");
    }

    public Deflater(int level) { this(level, false); }
    public Deflater() { this(DEFAULT_COMPRESSION, false); }

    public synchronized void setInput(byte[] b, int off, int len) {
        if (b == null) throw new NullPointerException();
        if (off < 0 || len < 0 || off > b.length - len) throw new ArrayIndexOutOfBoundsException();
        this.buf = b;
        this.off = off;
        this.len = len;
    }

    public synchronized void setInput(byte[] b) { setInput(b, 0, b.length); }
    public synchronized void finish() { finish = true; }
    public synchronized boolean finished() { return finished; }
    public synchronized boolean needsInput() { return len <= 0; }
    public synchronized int getTotalIn() { return (int) totalIn; }
    public synchronized int getTotalOut() { return (int) totalOut; }
    public synchronized long getBytesRead() { return totalIn; }
    public synchronized long getBytesWritten() { return totalOut; }
    public synchronized void setLevel(int level) {}
    public synchronized void setStrategy(int strategy) {}

    public synchronized int deflate(byte[] b, int off, int len, int flush) {
        if (b == null) throw new NullPointerException();
        if (off < 0 || len < 0 || off > b.length - len) throw new ArrayIndexOutOfBoundsException();
        if (handle == 0) throw new NullPointerException("Deflater has been closed");
        if (finished || len == 0) return 0;
        int mode = finish ? KuZlib.FINISH : flush;
        int status = KuZlib.deflate(handle, buf, this.off, this.len, b, off, len, mode, counts);
        this.off += counts[0];
        this.len -= counts[0];
        totalIn += counts[0];
        totalOut += counts[1];
        if (status == 1) finished = true;
        if (status < 0) throw new IllegalStateException("deflate failed");
        return counts[1];
    }

    public synchronized int deflate(byte[] b, int off, int len) { return deflate(b, off, len, NO_FLUSH); }
    public synchronized int deflate(byte[] b) { return deflate(b, 0, b.length, NO_FLUSH); }

    public synchronized void reset() {
        if (handle == 0) throw new NullPointerException("Deflater has been closed");
        KuZlib.deflateReset(handle);
        buf = EMPTY;
        off = 0;
        len = 0;
        finish = false;
        finished = false;
        totalIn = 0;
        totalOut = 0;
    }

    public synchronized void end() {
        if (handle != 0) {
            KuZlib.deflateEnd(handle);
            handle = 0;
        }
    }
}
