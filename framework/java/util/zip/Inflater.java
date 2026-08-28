package java.util.zip;

public class Inflater {
    private byte[] buf = new byte[0];
    private int off, len;
    private boolean finished;

    public Inflater(boolean nowrap) {}
    public Inflater() { this(false); }

    public synchronized void setInput(byte[] b, int off, int len) {
        if (b == null) throw new NullPointerException();
        if (off < 0 || len < 0 || off > b.length - len) throw new ArrayIndexOutOfBoundsException();
        this.buf = b;
        this.off = off;
        this.len = len;
    }
    public synchronized void setInput(byte[] b) {
        setInput(b, 0, b.length);
    }
    public synchronized int inflate(byte[] b, int off, int len) throws DataFormatException {
        if (b == null) throw new NullPointerException();
        if (off < 0 || len < 0 || off > b.length - len) throw new ArrayIndexOutOfBoundsException();
        int copy = Math.min(len, this.len);
        if (copy > 0) {
            System.arraycopy(this.buf, this.off, b, off, copy);
            this.off += copy;
            this.len -= copy;
            if (this.len == 0) finished = true;
            return copy;
        }
        return 0;
    }
    public synchronized int inflate(byte[] b) throws DataFormatException {
        return inflate(b, 0, b.length);
    }
    public synchronized boolean finished() { return finished; }
    public synchronized boolean needsInput() { return len <= 0; }
    public synchronized void reset() { finished = false; len = 0; }
    public synchronized void end() {}
}
