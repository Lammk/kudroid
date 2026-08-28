package java.io;

public class InputStreamReader extends Reader {
    private final InputStream in;

    public InputStreamReader(InputStream in) {
        this.in = in;
    }
    public InputStreamReader(InputStream in, String charsetName) {
        this.in = in;
    }
    public int read(char[] cbuf, int off, int len) throws IOException {
        if (in == null) return -1;
        byte[] b = new byte[len];
        int n = in.read(b, 0, len);
        if (n <= 0) return n;
        for (int i = 0; i < n; i++) {
            cbuf[off + i] = (char)(b[i] & 0xff);
        }
        return n;
    }
    public void close() throws IOException {
        if (in != null) in.close();
    }
}
