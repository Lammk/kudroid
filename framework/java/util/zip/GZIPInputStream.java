package java.util.zip;

import java.io.EOFException;
import java.io.IOException;
import java.io.InputStream;

public class GZIPInputStream extends InflaterInputStream {
    public static final int GZIP_MAGIC = 0x8b1f;

    private static final int FHCRC = 2;
    private static final int FEXTRA = 4;
    private static final int FNAME = 8;
    private static final int FCOMMENT = 16;

    protected CRC32 crc = new CRC32();
    protected boolean eos;
    private boolean closed;

    public GZIPInputStream(InputStream in, int size) throws IOException {
        super(in, new Inflater(true), size);
        readHeader(in);
    }

    public GZIPInputStream(InputStream in) throws IOException {
        this(in, 512);
    }

    public int read(byte[] b, int off, int len) throws IOException {
        if (closed) throw new IOException("Stream closed");
        if (eos) return -1;
        int n = super.read(b, off, len);
        if (n == -1) {
            if (readTrailer()) {
                eos = true;
            } else {
                return read(b, off, len);  // another member follows
            }
        } else {
            crc.update(b, off, n);
        }
        return n;
    }

    public void close() throws IOException {
        if (!closed) {
            super.close();
            eos = true;
            closed = true;
        }
    }

    private int readUByte(InputStream in) throws IOException {
        int b = in.read();
        if (b == -1) throw new EOFException();
        return b;
    }

    private int readUShort(InputStream in) throws IOException {
        int b = readUByte(in);
        return (readUByte(in) << 8) | b;
    }

    private long readUInt(InputStream in) throws IOException {
        long s = readUShort(in);
        return ((long) readUShort(in) << 16) | s;
    }

    private void readHeader(InputStream src) throws IOException {
        CRC32 header = new CRC32();
        if (readUShort(src) != GZIP_MAGIC) throw new ZipException("Not in GZIP format");
        if (readUByte(src) != 8) throw new ZipException("Unsupported compression method");
        int flags = readUByte(src);
        for (int i = 0; i < 6; i++) readUByte(src);  // mtime, extra flags, OS
        if ((flags & FEXTRA) != 0) {
            int n = readUShort(src);
            for (int i = 0; i < n; i++) readUByte(src);
        }
        if ((flags & FNAME) != 0) while (readUByte(src) != 0) { }
        if ((flags & FCOMMENT) != 0) while (readUByte(src) != 0) { }
        if ((flags & FHCRC) != 0) readUShort(src);
        crc.reset();
    }

    /** Checks the CRC and length; true at the end of the stream, false if another member follows. */
    private boolean readTrailer() throws IOException {
        InputStream trailerIn = in;
        int remaining = inf.getRemaining();
        if (remaining > 0) {
            trailerIn = new java.io.SequenceInputStream(
                    new java.io.ByteArrayInputStream(buf, len - remaining, remaining), in);
        }
        if (readUInt(trailerIn) != crc.getValue() || readUInt(trailerIn) != (inf.getBytesWritten() & 0xffffffffL)) {
            throw new ZipException("Corrupt GZIP trailer");
        }
        // A concatenated member would start with the magic number; anything else is trailing data.
        if (remaining > 8 || in.available() > 0) {
            InputStream next = remaining > 8
                    ? new java.io.SequenceInputStream(
                            new java.io.ByteArrayInputStream(buf, len - remaining + 8, remaining - 8), in)
                    : in;
            try {
                readHeader(next);
            } catch (IOException e) {
                return true;
            }
            inf.reset();
            if (remaining > 8) inf.setInput(buf, len - remaining + 8 + 10, 0);
            return false;
        }
        return true;
    }
}
