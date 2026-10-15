package java.io;

import java.nio.CharBuffer;

public abstract class Reader implements Readable, Closeable {
    protected Object lock;
    protected Reader() { this.lock = this; }
    protected Reader(Object lock) {
        if (lock == null) throw new NullPointerException();
        this.lock = lock;
    }
    public int read() throws IOException {
        char[] cb = new char[1];
        if (read(cb, 0, 1) == -1) return -1;
        return cb[0];
    }
    public int read(char[] cbuf) throws IOException {
        return read(cbuf, 0, cbuf.length);
    }
    public abstract int read(char[] cbuf, int off, int len) throws IOException;
    public int read(CharBuffer target) throws IOException {
        return 0;
    }
    public long skip(long n) throws IOException { return 0; }
    public boolean ready() throws IOException { return false; }
    public boolean markSupported() { return false; }
    public void mark(int readAheadLimit) throws IOException {}
    public void reset() throws IOException {}
    public abstract void close() throws IOException;
}
