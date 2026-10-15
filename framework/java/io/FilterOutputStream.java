package java.io;

public class FilterOutputStream extends OutputStream {
    protected OutputStream out;

    public FilterOutputStream(OutputStream out) {
        this.out = out;
    }
    public void write(int b) throws IOException {
        if (out != null) out.write(b);
    }
    public void write(byte[] b, int off, int len) throws IOException {
        if (out != null) out.write(b, off, len);
    }
    public void flush() throws IOException {
        if (out != null) out.flush();
    }
    public void close() throws IOException {
        if (out != null) out.close();
    }
}
