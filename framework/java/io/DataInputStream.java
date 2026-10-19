package java.io;

public class DataInputStream extends FilterInputStream implements DataInput {
    private final byte[] work = new byte[8];

    public DataInputStream(InputStream in) {
        super(in);
    }

    public final int read(byte[] b) throws IOException {
        return in.read(b, 0, b.length);
    }

    public final int read(byte[] b, int off, int len) throws IOException {
        return in.read(b, off, len);
    }

    public final void readFully(byte[] b) throws IOException {
        readFully(b, 0, b.length);
    }

    public final void readFully(byte[] b, int off, int len) throws IOException {
        if (len < 0) {
            throw new IndexOutOfBoundsException();
        }
        int done = 0;
        while (done < len) {
            final int n = in.read(b, off + done, len - done);
            if (n < 0) {
                throw new EOFException();
            }
            done += n;
        }
    }

    public final int skipBytes(int n) throws IOException {
        int skipped = 0;
        while (skipped < n) {
            final long s = in.skip(n - skipped);
            if (s <= 0) {
                break;
            }
            skipped += (int) s;
        }
        return skipped;
    }

    public final boolean readBoolean() throws IOException {
        return readUnsignedByte() != 0;
    }

    public final byte readByte() throws IOException {
        return (byte) readUnsignedByte();
    }

    public final int readUnsignedByte() throws IOException {
        final int c = in.read();
        if (c < 0) {
            throw new EOFException();
        }
        return c;
    }

    public final short readShort() throws IOException {
        return (short) readUnsignedShort();
    }

    public final int readUnsignedShort() throws IOException {
        readFully(work, 0, 2);
        return ((work[0] & 0xff) << 8) | (work[1] & 0xff);
    }

    public final char readChar() throws IOException {
        return (char) readUnsignedShort();
    }

    public final int readInt() throws IOException {
        readFully(work, 0, 4);
        return ((work[0] & 0xff) << 24) | ((work[1] & 0xff) << 16)
                | ((work[2] & 0xff) << 8) | (work[3] & 0xff);
    }

    public final long readLong() throws IOException {
        readFully(work, 0, 8);
        long v = 0;
        for (int i = 0; i < 8; i++) {
            v = (v << 8) | (work[i] & 0xffL);
        }
        return v;
    }

    public final float readFloat() throws IOException {
        return Float.intBitsToFloat(readInt());
    }

    public final double readDouble() throws IOException {
        return Double.longBitsToDouble(readLong());
    }

    /** Reads a line of Latin-1 text; "\n", "\r" and "\r\n" all terminate it. */
    public final String readLine() throws IOException {
        final StringBuilder sb = new StringBuilder();
        boolean any = false;
        while (true) {
            final int c = in.read();
            if (c < 0) {
                return any ? sb.toString() : null;
            }
            any = true;
            if (c == '\n') {
                return sb.toString();
            }
            if (c == '\r') {
                // A "\r\n" pair is one terminator; a lone "\r" has nothing left to consume.
                if (in.markSupported()) {
                    in.mark(1);
                    final int next = in.read();
                    if (next != '\n' && next >= 0) {
                        in.reset();
                    }
                }
                return sb.toString();
            }
            sb.append((char) c);
        }
    }

    public final String readUTF() throws IOException {
        return readUTF(this);
    }

    /** Decodes a modified UTF-8 string preceded by its unsigned 16-bit byte length. */
    public static final String readUTF(DataInput in) throws IOException {
        final int length = in.readUnsignedShort();
        final byte[] bytes = new byte[length];
        in.readFully(bytes, 0, length);
        final char[] chars = new char[length];
        int count = 0;
        int i = 0;
        while (i < length) {
            final int a = bytes[i] & 0xff;
            if (a < 0x80) {
                chars[count++] = (char) a;
                i++;
            } else if ((a & 0xe0) == 0xc0) {
                if (i + 1 >= length) {
                    throw new UTFDataFormatException("malformed input: partial character at end");
                }
                final int b = bytes[i + 1];
                if ((b & 0xc0) != 0x80) {
                    throw new UTFDataFormatException("malformed input around byte " + (i + 1));
                }
                chars[count++] = (char) (((a & 0x1f) << 6) | (b & 0x3f));
                i += 2;
            } else if ((a & 0xf0) == 0xe0) {
                if (i + 2 >= length) {
                    throw new UTFDataFormatException("malformed input: partial character at end");
                }
                final int b = bytes[i + 1];
                final int c = bytes[i + 2];
                if ((b & 0xc0) != 0x80 || (c & 0xc0) != 0x80) {
                    throw new UTFDataFormatException("malformed input around byte " + (i + 1));
                }
                chars[count++] = (char) (((a & 0x0f) << 12) | ((b & 0x3f) << 6) | (c & 0x3f));
                i += 3;
            } else {
                throw new UTFDataFormatException("malformed input around byte " + i);
            }
        }
        return new String(chars, 0, count);
    }
}
