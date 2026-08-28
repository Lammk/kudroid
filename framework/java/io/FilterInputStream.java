package java.io;

public class FilterInputStream extends InputStream {
    protected volatile InputStream in;

    protected FilterInputStream(InputStream in) {
        this.in = in;
    }
    public int read() throws IOException {
        return in != null ? in.read() : -1;
    }
    public int read(byte[] b, int off, int len) throws IOException {
        return in != null ? in.read(b, off, len) : -1;
    }
    public long skip(long n) throws IOException {
        return in != null ? in.skip(n) : 0;
    }
    public int available() throws IOException {
        return in != null ? in.available() : 0;
    }
    public void close() throws IOException {
        if (in != null) in.close();
    }
}
