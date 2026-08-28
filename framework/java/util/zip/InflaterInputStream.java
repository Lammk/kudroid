package java.util.zip;

import java.io.FilterInputStream;
import java.io.InputStream;
import java.io.IOException;

public class InflaterInputStream extends FilterInputStream {
    protected Inflater inf;
    protected byte[] buf;
    protected int len;

    public InflaterInputStream(InputStream in, Inflater inf, int size) {
        super(in);
        if (in == null || inf == null) throw new NullPointerException();
        if (size <= 0) throw new IllegalArgumentException("size <= 0");
        this.inf = inf;
        buf = new byte[size];
    }
    public InflaterInputStream(InputStream in, Inflater inf) { this(in, inf, 512); }
    public InflaterInputStream(InputStream in) { this(in, new Inflater()); }

    public int read() throws IOException {
        byte[] b = new byte[1];
        if (read(b, 0, 1) == -1) return -1;
        return b[0] & 0xff;
    }

    public int read(byte[] b, int off, int len) throws IOException {
        return in.read(b, off, len);
    }
}
