package java.io;

/** Reads one stream to its end, then the next. */
public class SequenceInputStream extends InputStream {
    private InputStream first;
    private final InputStream second;

    public SequenceInputStream(InputStream s1, InputStream s2) {
        if (s1 == null || s2 == null) throw new NullPointerException();
        first = s1;
        second = s2;
    }

    private InputStream current() { return first != null ? first : second; }

    @Override
    public int available() throws IOException { return current().available(); }

    @Override
    public int read() throws IOException {
        int c = current().read();
        if (c == -1 && first != null) {
            first = null;
            c = second.read();
        }
        return c;
    }

    @Override
    public int read(byte[] b, int off, int len) throws IOException {
        if (len == 0) return 0;
        int n = current().read(b, off, len);
        if (n <= 0 && first != null) {
            first = null;
            n = second.read(b, off, len);
        }
        return n;
    }

    @Override
    public void close() throws IOException {
        if (first != null) first.close();
        second.close();
    }
}
