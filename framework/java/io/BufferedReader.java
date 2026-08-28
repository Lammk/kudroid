package java.io;

public class BufferedReader extends Reader {
    private final Reader in;

    public BufferedReader(Reader in) {
        this.in = in;
    }
    public BufferedReader(Reader in, int sz) {
        this.in = in;
    }
    public int read(char[] cbuf, int off, int len) throws IOException {
        return in.read(cbuf, off, len);
    }
    public String readLine() throws IOException {
        StringBuilder sb = new StringBuilder();
        int c;
        char[] buf = new char[1];
        while (in.read(buf, 0, 1) > 0) {
            c = buf[0];
            if (c == '\n') break;
            if (c == '\r') {
                continue;
            }
            sb.append((char)c);
        }
        return (sb.length() == 0 && buf[0] == 0) ? null : sb.toString();
    }
    public void close() throws IOException {
        in.close();
    }
}
