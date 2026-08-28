package java.io;

public class BufferedWriter extends Writer {
    private final Writer out;

    public BufferedWriter(Writer out) {
        this.out = out;
    }
    public BufferedWriter(Writer out, int sz) {
        this.out = out;
    }
    public void write(char[] cbuf, int off, int len) throws IOException {
        out.write(cbuf, off, len);
    }
    public void newLine() throws IOException {
        out.write("\n");
    }
    public void flush() throws IOException {
        out.flush();
    }
    public void close() throws IOException {
        out.close();
    }
}
