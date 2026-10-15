package java.util.zip;

import libcore.io.KuZlib;

public class Inflater {
    private static final byte[] EMPTY = new byte[0];

    private long handle;
    private byte[] buf = EMPTY;
    private int off;
    private int len;
    private boolean finished;
    private boolean needDict;
    private long totalIn;
    private long totalOut;
    private final int[] counts = new int[2];

    public Inflater(boolean nowrap) {
        handle = KuZlib.inflateInit(nowrap);
        if (handle == 0) throw new OutOfMemoryError("could not allocate an inflater");
    }

    public Inflater() { this(false); }

    public synchronized void setInput(byte[] b, int off, int len) {
        if (b == null) throw new NullPointerException();
        if (off < 0 || len < 0 || off > b.length - len) throw new ArrayIndexOutOfBoundsException();
        this.buf = b;
        this.off = off;
        this.len = len;
    }

    public synchronized void setInput(byte[] b) { setInput(b, 0, b.length); }

    public synchronized int inflate(byte[] b, int off, int len) throws DataFormatException {
        if (b == null) throw new NullPointerException();
        if (off < 0 || len < 0 || off > b.length - len) throw new ArrayIndexOutOfBoundsException();
        if (handle == 0) throw new NullPointerException("Inflater has been closed");
        if (finished || needDict || len == 0) return 0;
        int status = KuZlib.inflate(handle, buf, this.off, this.len, b, off, len, 0, counts);
        this.off += counts[0];
        this.len -= counts[0];
        totalIn += counts[0];
        totalOut += counts[1];
        if (status == 1) finished = true;
        if (status == 2) needDict = true;
        if (status < 0) throw new DataFormatException(status == -4 ? "out of memory" : "invalid stream or data");
        return counts[1];
    }

    public synchronized int inflate(byte[] b) throws DataFormatException { return inflate(b, 0, b.length); }
    public synchronized boolean finished() { return finished; }
    public synchronized boolean needsInput() { return len <= 0 && !finished; }
    public synchronized boolean needsDictionary() { return needDict; }
    public synchronized int getRemaining() { return len; }
    public synchronized int getTotalIn() { return (int) totalIn; }
    public synchronized int getTotalOut() { return (int) totalOut; }
    public synchronized long getBytesRead() { return totalIn; }
    public synchronized long getBytesWritten() { return totalOut; }

    public synchronized void reset() {
        if (handle == 0) throw new NullPointerException("Inflater has been closed");
        KuZlib.inflateReset(handle);
        buf = EMPTY;
        off = 0;
        len = 0;
        finished = false;
        needDict = false;
        totalIn = 0;
        totalOut = 0;
    }

    public synchronized void end() {
        if (handle != 0) {
            KuZlib.inflateEnd(handle);
            handle = 0;
        }
    }
}
