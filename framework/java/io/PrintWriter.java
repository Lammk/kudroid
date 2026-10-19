package java.io;

public class PrintWriter extends Writer {
    protected Writer out;
    private final boolean autoFlush;
    private boolean trouble;

    public PrintWriter(Writer out) {
        this(out, false);
    }

    public PrintWriter(Writer out, boolean autoFlush) {
        super(out);
        this.out = out;
        this.autoFlush = autoFlush;
    }

    public PrintWriter(OutputStream out) {
        this(out, false);
    }

    public PrintWriter(OutputStream out, boolean autoFlush) {
        this(new OutputStreamWriter(out), autoFlush);
    }

    public void write(int c) {
        write(new char[] {(char) c}, 0, 1);
    }

    public void write(char[] buf, int off, int len) {
        if (out == null) return;
        try {
            out.write(buf, off, len);
        } catch (IOException e) {
            trouble = true;
        }
    }

    public void write(char[] buf) {
        write(buf, 0, buf.length);
    }

    public void write(String s, int off, int len) {
        char[] chars = new char[len];
        for (int i = 0; i < len; i++) chars[i] = s.charAt(off + i);
        write(chars, 0, len);
    }

    public void write(String s) {
        write(s, 0, s.length());
    }

    public void flush() {
        if (out == null) return;
        try {
            out.flush();
        } catch (IOException e) {
            trouble = true;
        }
    }

    public void close() {
        if (out == null) return;
        try {
            out.close();
        } catch (IOException e) {
            trouble = true;
        }
        out = null;
    }

    public boolean checkError() {
        if (out != null) flush();
        return trouble;
    }

    public void print(boolean b) { write(String.valueOf(b)); }
    public void print(char c) { write(c); }
    public void print(int i) { write(String.valueOf(i)); }
    public void print(long l) { write(String.valueOf(l)); }
    public void print(float f) { write(String.valueOf(f)); }
    public void print(double d) { write(String.valueOf(d)); }
    public void print(char[] s) { write(s); }
    public void print(String s) { write(s == null ? "null" : s); }
    public void print(Object obj) { write(String.valueOf(obj)); }

    public void println() {
        write("\n");
        if (autoFlush) flush();
    }

    public void println(boolean x) { print(x); println(); }
    public void println(char x) { print(x); println(); }
    public void println(int x) { print(x); println(); }
    public void println(long x) { print(x); println(); }
    public void println(float x) { print(x); println(); }
    public void println(double x) { print(x); println(); }
    public void println(char[] x) { print(x); println(); }
    public void println(String x) { print(x); println(); }
    public void println(Object x) { print(String.valueOf(x)); println(); }

    public PrintWriter append(CharSequence csq) {
        write(String.valueOf(csq));
        return this;
    }

    public PrintWriter append(char c) {
        write(c);
        return this;
    }
}
