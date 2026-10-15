package java.util.zip;

import java.io.FilterOutputStream;
import java.io.IOException;
import java.io.OutputStream;

public class DeflaterOutputStream extends FilterOutputStream {
    protected Deflater def;
    protected byte[] buf;
    private boolean closed;
    private final boolean syncFlush;
    boolean usesDefaultDeflater;

    public DeflaterOutputStream(OutputStream out, Deflater def, int size, boolean syncFlush) {
        super(out);
        if (out == null || def == null) throw new NullPointerException();
        if (size <= 0) throw new IllegalArgumentException("size <= 0");
        this.def = def;
        this.syncFlush = syncFlush;
        buf = new byte[size];
    }

    public DeflaterOutputStream(OutputStream out, Deflater def, int size) { this(out, def, size, false); }
    public DeflaterOutputStream(OutputStream out, Deflater def) { this(out, def, 512, false); }
    public DeflaterOutputStream(OutputStream out, boolean syncFlush) {
        this(out, new Deflater(), 512, syncFlush);
        usesDefaultDeflater = true;
    }
    public DeflaterOutputStream(OutputStream out) {
        this(out, false);
        usesDefaultDeflater = true;
    }

    public void write(int b) throws IOException {
        byte[] one = new byte[1];
        one[0] = (byte) (b & 0xff);
        write(one, 0, 1);
    }

    public void write(byte[] b, int off, int len) throws IOException {
        if (def.finished()) throw new IOException("write beyond end of stream");
        if ((off | len | (off + len) | (b.length - (off + len))) < 0) throw new IndexOutOfBoundsException();
        if (len == 0) return;
        def.setInput(b, off, len);
        while (!def.needsInput()) deflate();
    }

    protected void deflate() throws IOException {
        int n = def.deflate(buf, 0, buf.length);
        if (n > 0) out.write(buf, 0, n);
    }

    public void finish() throws IOException {
        if (!def.finished()) {
            def.finish();
            while (!def.finished()) deflate();
        }
    }

    public void flush() throws IOException {
        if (syncFlush && !def.finished()) {
            int n;
            while ((n = def.deflate(buf, 0, buf.length, Deflater.SYNC_FLUSH)) > 0) {
                out.write(buf, 0, n);
                if (n < buf.length) break;
            }
        }
        out.flush();
    }

    public void close() throws IOException {
        if (!closed) {
            finish();
            if (usesDefaultDeflater) def.end();
            out.close();
            closed = true;
        }
    }
}
