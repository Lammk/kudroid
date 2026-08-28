package java.util.zip;

import java.io.FilterOutputStream;
import java.io.OutputStream;
import java.io.IOException;

public class DeflaterOutputStream extends FilterOutputStream {
    protected Deflater def;
    protected byte[] buf;

    public DeflaterOutputStream(OutputStream out, Deflater def, int size, boolean syncFlush) {
        super(out);
        if (out == null || def == null) throw new NullPointerException();
        if (size <= 0) throw new IllegalArgumentException("size <= 0");
        this.def = def;
        buf = new byte[size];
    }
    public DeflaterOutputStream(OutputStream out, Deflater def, int size) { this(out, def, size, false); }
    public DeflaterOutputStream(OutputStream out, Deflater def) { this(out, def, 512, false); }
    public DeflaterOutputStream(OutputStream out) { this(out, new Deflater()); }

    public void write(int b) throws IOException {
        byte[] buf = new byte[1];
        buf[0] = (byte)(b & 0xff);
        write(buf, 0, 1);
    }
    public void write(byte[] b, int off, int len) throws IOException {
        out.write(b, off, len);
    }
    public void finish() throws IOException {}
}
