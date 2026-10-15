package java.io;

public class StringWriter extends Writer {
    private final StringBuffer buf = new StringBuffer();
    public StringWriter() {}
    public StringWriter(int initialSize) {}
    public void write(int c) { buf.append((char) c); }
    public void write(char[] cbuf, int off, int len) {
        if (cbuf == null) throw new NullPointerException();
        if (off < 0 || len < 0 || off + len > cbuf.length) throw new IndexOutOfBoundsException();
        buf.append(new String(cbuf, off, len));
    }
    public void write(String str) { buf.append(str); }
    public void write(String str, int off, int len) {
        buf.append(str.substring(off, off + len));
    }
    public StringWriter append(CharSequence csq) {
        if (csq == null) write("null");
        else write(csq.toString());
        return this;
    }
    public String toString() { return buf.toString(); }
    public StringBuffer getBuffer() { return buf; }
    public void flush() {}
    public void close() throws IOException {}
}
