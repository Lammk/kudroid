package java.util.zip;

import java.io.EOFException;
import java.io.FilterInputStream;
import java.io.IOException;
import java.io.InputStream;

public class InflaterInputStream extends FilterInputStream {
    protected Inflater inf;
    protected byte[] buf;
    protected int len;
    private boolean closed;
    private boolean reachedEnd;

    public InflaterInputStream(InputStream in, Inflater inf, int size) {
        super(in);
        if (in == null || inf == null) throw new NullPointerException();
        if (size <= 0) throw new IllegalArgumentException("size <= 0");
        this.inf = inf;
        buf = new byte[size];
    }

    public InflaterInputStream(InputStream in, Inflater inf) { this(in, inf, 512); }
    public InflaterInputStream(InputStream in) { this(in, new Inflater()); }

    private void ensureOpen() throws IOException {
        if (closed) throw new IOException("Stream closed");
    }

    public int read() throws IOException {
        byte[] b = new byte[1];
        if (read(b, 0, 1) == -1) return -1;
        return b[0] & 0xff;
    }

    public int read(byte[] b, int off, int len) throws IOException {
        ensureOpen();
        if (b == null) throw new NullPointerException();
        if (off < 0 || len < 0 || len > b.length - off) throw new IndexOutOfBoundsException();
        if (len == 0) return 0;
        try {
            int n;
            while ((n = inf.inflate(b, off, len)) == 0) {
                if (inf.finished() || inf.needsDictionary()) {
                    reachedEnd = true;
                    return -1;
                }
                if (inf.needsInput()) fill();
            }
            return n;
        } catch (DataFormatException e) {
            String s = e.getMessage();
            throw new ZipException(s != null ? s : "Invalid ZLIB data format");
        }
    }

    protected void fill() throws IOException {
        ensureOpen();
        len = in.read(buf, 0, buf.length);
        if (len == -1) throw new EOFException("Unexpected end of ZLIB input stream");
        inf.setInput(buf, 0, len);
    }

    public int available() throws IOException {
        ensureOpen();
        return reachedEnd ? 0 : 1;
    }

    public long skip(long n) throws IOException {
        if (n < 0) throw new IllegalArgumentException("negative skip length");
        ensureOpen();
        byte[] scratch = new byte[(int) Math.min(n, 512L)];
        long total = 0;
        while (total < n) {
            int r = read(scratch, 0, (int) Math.min(n - total, (long) scratch.length));
            if (r < 0) break;
            total += r;
        }
        return total;
    }

    public void close() throws IOException {
        if (!closed) {
            inf.end();
            closed = true;
            in.close();
        }
    }

    public boolean markSupported() { return false; }
    public synchronized void mark(int readlimit) {}
    public synchronized void reset() throws IOException { throw new IOException("mark/reset not supported"); }
}
