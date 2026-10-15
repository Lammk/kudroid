package java.io;

public abstract class Writer implements Appendable, Closeable, Flushable {
    protected Object lock;
    protected Writer() { this.lock = this; }
    protected Writer(Object lock) {
        if (lock == null) throw new NullPointerException();
        this.lock = lock;
    }
    public void write(int c) throws IOException {}
    public void write(char[] cbuf) throws IOException { write(cbuf, 0, cbuf.length); }
    public abstract void write(char[] cbuf, int off, int len) throws IOException;
    public void write(String str) throws IOException { write(str, 0, str.length()); }
    public void write(String str, int off, int len) throws IOException {}
    public Writer append(CharSequence csq) throws IOException { return this; }
    public Writer append(CharSequence csq, int start, int end) throws IOException { return this; }
    public Writer append(char c) throws IOException { return this; }
    public abstract void flush() throws IOException;
    public abstract void close() throws IOException;
}
