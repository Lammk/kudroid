package java.io;

public class StringReader extends Reader {
    private final String str;
    private final int length;
    private int next = 0;

    public StringReader(String s) {
        this.str = s;
        this.length = s.length();
    }
    public int read() throws IOException {
        if (next >= length) return -1;
        return str.charAt(next++);
    }
    public int read(char[] cbuf, int off, int len) throws IOException {
        if (next >= length) return -1;
        int n = Math.min(length - next, len);
        str.getChars(next, next + n, cbuf, off);
        next += n;
        return n;
    }
    public void close() {}
}
