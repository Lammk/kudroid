package java.util.zip;

import java.io.IOException;
import java.io.OutputStream;

public class GZIPOutputStream extends DeflaterOutputStream {
    protected CRC32 crc = new CRC32();

    public GZIPOutputStream(OutputStream out, int size, boolean syncFlush) throws IOException {
        super(out, new Deflater(Deflater.DEFAULT_COMPRESSION, true), size, syncFlush);
        usesDefaultDeflater = true;
        byte[] header = {
            (byte) 0x1f, (byte) 0x8b, 8, 0, 0, 0, 0, 0, 0, (byte) 0xff
        };
        out.write(header, 0, header.length);
    }

    public GZIPOutputStream(OutputStream out, int size) throws IOException { this(out, size, false); }
    public GZIPOutputStream(OutputStream out) throws IOException { this(out, 512, false); }
    public GZIPOutputStream(OutputStream out, boolean syncFlush) throws IOException { this(out, 512, syncFlush); }

    public synchronized void write(byte[] b, int off, int len) throws IOException {
        super.write(b, off, len);
        crc.update(b, off, len);
    }

    public void finish() throws IOException {
        if (!def.finished()) {
            def.finish();
            while (!def.finished()) {
                int n = def.deflate(buf, 0, buf.length);
                if (def.finished() && n <= buf.length - 8) {
                    // The trailer fits behind the last block.
                    writeTrailer(buf, n);
                    out.write(buf, 0, n + 8);
                    return;
                }
                if (n > 0) out.write(buf, 0, n);
            }
            byte[] trailer = new byte[8];
            writeTrailer(trailer, 0);
            out.write(trailer, 0, 8);
        }
    }

    private void writeTrailer(byte[] b, int offset) {
        writeInt((int) crc.getValue(), b, offset);
        writeInt(def.getTotalIn(), b, offset + 4);
    }

    private static void writeInt(int v, byte[] b, int off) {
        b[off] = (byte) v;
        b[off + 1] = (byte) (v >>> 8);
        b[off + 2] = (byte) (v >>> 16);
        b[off + 3] = (byte) (v >>> 24);
    }
}
