package java.util.zip;

public class Deflater {
    public static final int DEFLATED = 8;
    public static final int NO_COMPRESSION = 0;
    public static final int BEST_SPEED = 1;
    public static final int BEST_COMPRESSION = 9;
    public static final int DEFAULT_COMPRESSION = -1;

    private byte[] buf = new byte[0];
    private int off, len;
    private boolean finish;
    private boolean finished;

    public Deflater(int level, boolean nowrap) {}
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
    public synchronized int deflate(byte[] b, int off, int len) {
        if (b == null) throw new NullPointerException();
        if (off < 0 || len < 0 || off > b.length - len) throw new ArrayIndexOutOfBoundsException();
        int copy = Math.min(len, this.len);
        if (copy > 0) {
            System.arraycopy(this.buf, this.off, b, off, copy);
            this.off += copy;
            this.len -= copy;
            if (this.len == 0 && finish) finished = true;
            return copy;
        }
        return 0;
    }
    public synchronized int deflate(byte[] b) { return deflate(b, 0, b.length); }
    public synchronized boolean needsInput() { return len <= 0; }
    public synchronized void reset() { finish = false; finished = false; len = 0; }
    public synchronized void end() {}
}
