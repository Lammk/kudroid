package java.io;

/** Ghi ra fd của POSIX qua native; fd 1 = stdout, 2 = stderr. */
public class PrintStream extends OutputStream {

    private final int fd;
    private final ByteArrayOutputStream line = new ByteArrayOutputStream(128);

    public PrintStream(int fd) {
        this.fd = fd;
    }

    public PrintStream(OutputStream out) {
        this.fd = 1;
    }

    public void write(int b) {
        line.write(b);
        if (b == '\n') {
            flush();
        }
    }

    public void write(byte[] b, int off, int len) {
        for (int i = 0; i < len; i++) {
            write(b[off + i]);
        }
    }

    public void flush() {
        if (line.size() == 0) {
            return;
        }
        writeNative(fd, line.toByteArray());
        line.reset();
    }

    public void close() {
        flush();
    }

    public void print(String s) {
        String v = s == null ? "null" : s;
        byte[] bytes = v.getBytes();
        for (int i = 0; i < bytes.length; i++) {
            write(bytes[i]);
        }
    }

    public void print(Object o) {
        print(o == null ? "null" : o.toString());
    }

    public void print(char c) {
        print(String.valueOf(c));
    }

    public void print(boolean b) {
        print(b ? "true" : "false");
    }

    public void print(int i) {
        print(Integer.toString(i));
    }

    public void print(long l) {
        print(Long.toString(l));
    }

    public void print(float f) {
        print(Float.toString(f));
    }

    public void print(double d) {
        print(Double.toString(d));
    }

    public void print(char[] chars) {
        print(new String(chars));
    }

    public void println() {
        write('\n');
    }

    public void println(String s) {
        print(s);
        println();
    }

    public void println(Object o) {
        print(o);
        println();
    }

    public void println(char c) {
        print(c);
        println();
    }

    public void println(boolean b) {
        print(b);
        println();
    }

    public void println(int i) {
        print(i);
        println();
    }

    public void println(long l) {
        print(l);
        println();
    }

    public void println(float f) {
        print(f);
        println();
    }

    public void println(double d) {
        print(d);
        println();
    }

    public void println(char[] chars) {
        print(chars);
        println();
    }

    public PrintStream printf(String format, Object... args) {
        print(String.format(format, args));
        return this;
    }

    public PrintStream format(String format, Object... args) {
        return printf(format, args);
    }

    public PrintStream append(CharSequence csq) {
        print(csq == null ? "null" : csq.toString());
        return this;
    }

    public boolean checkError() {
        return false;
    }

    private static native void writeNative(int fd, byte[] data);
}
