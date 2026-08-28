package java.util;

import java.io.InputStream;
import java.io.Reader;
import java.io.StringReader;
import java.io.Closeable;
import java.io.InputStreamReader;

public final class Scanner implements Iterator<String>, Closeable {
    private final StringTokenizer tokens;

    public Scanner(InputStream source) {
        this(new InputStreamReader(source));
    }
    public Scanner(Reader source) {
        this("");
    }
    public Scanner(String source) {
        tokens = new StringTokenizer(source);
    }
    public boolean hasNext() { return tokens.hasMoreTokens(); }
    public String next() { return tokens.nextToken(); }
    public boolean hasNextInt() { return hasNext(); }
    public int nextInt() { return Integer.parseInt(next()); }
    public boolean hasNextLong() { return hasNext(); }
    public long nextLong() { return Long.parseLong(next()); }
    public boolean hasNextDouble() { return hasNext(); }
    public double nextDouble() { return Double.parseDouble(next()); }
    public boolean hasNextLine() { return hasNext(); }
    public String nextLine() { return next(); }
    public void remove() { throw new UnsupportedOperationException(); }
    public void close() {}
}
