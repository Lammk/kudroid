package java.io;

public class OutputStreamWriter extends Writer {
    private final OutputStream out;

    public OutputStreamWriter(OutputStream out) {
        this.out = out;
    }
    public OutputStreamWriter(OutputStream out, String charsetName) {
        this.out = out;
    }
    public void write(char[] cbuf, int off, int len) throws IOException {
        if (out == null) return;
        byte[] b = new byte[len];
        for (int i = 0; i < len; i++) {
            b[i] = (byte)(cbuf[off + i] & 0xff);
        }
        out.write(b, 0, len);
    }
    public void flush() throws IOException {
        if (out != null) out.flush();
    }
    public void close() throws IOException {
        if (out != null) out.close();
    }
}
