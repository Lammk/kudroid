package java.io;

public class BufferedInputStream extends FilterInputStream {
    private static final int DEFAULT_SIZE = 8192;

    protected volatile byte[] buf;
    protected int count;
    protected int pos;
    protected int markpos = -1;
    protected int marklimit;

    public BufferedInputStream(InputStream in) {
        this(in, DEFAULT_SIZE);
    }

    public BufferedInputStream(InputStream in, int size) {
        super(in);
        if (size <= 0) {
            throw new IllegalArgumentException("Buffer size <= 0");
        }
        buf = new byte[size];
    }

    private InputStream source() throws IOException {
        final InputStream s = in;
        if (s == null) {
            throw new IOException("Stream closed");
        }
        return s;
    }

    private byte[] buffer() throws IOException {
        final byte[] b = buf;
        if (b == null) {
            throw new IOException("Stream closed");
        }
        return b;
    }

    /** Refills the buffer, keeping the marked region when one is active. */
    private void fill() throws IOException {
        byte[] b = buffer();
        if (markpos < 0) {
            pos = 0;
        } else if (pos >= b.length) {
            if (markpos > 0) {
                final int keep = pos - markpos;
                System.arraycopy(b, markpos, b, 0, keep);
                pos = keep;
                markpos = 0;
            } else if (b.length >= marklimit) {
                markpos = -1;
                pos = 0;
            } else {
                final int grown = Math.min(Math.max(pos * 2, 1), Math.max(marklimit, 1));
                final byte[] bigger = new byte[grown];
                System.arraycopy(b, 0, bigger, 0, pos);
                buf = b = bigger;
            }
        }
        count = pos;
        final int n = source().read(b, pos, b.length - pos);
        if (n > 0) {
            count = pos + n;
        }
    }

    public synchronized int read() throws IOException {
        if (pos >= count) {
            fill();
            if (pos >= count) {
                return -1;
            }
        }
        return buffer()[pos++] & 0xff;
    }

    public synchronized int read(byte[] b, int off, int len) throws IOException {
        buffer();
        if ((off | len | (off + len) | (b.length - (off + len))) < 0) {
            throw new IndexOutOfBoundsException();
        }
        if (len == 0) {
            return 0;
        }
        int total = 0;
        while (total < len) {
            int avail = count - pos;
            if (avail <= 0) {
                // A large read with nothing to preserve skips the copy through the buffer.
                if (markpos < 0 && len - total >= buffer().length) {
                    final int n = source().read(b, off + total, len - total);
                    if (n <= 0) {
                        return total == 0 ? n : total;
                    }
                    return total + n;
                }
                fill();
                avail = count - pos;
                if (avail <= 0) {
                    return total == 0 ? -1 : total;
                }
            }
            final int chunk = Math.min(avail, len - total);
            System.arraycopy(buffer(), pos, b, off + total, chunk);
            pos += chunk;
            total += chunk;
            // Stop when the source has nothing more ready, as the contract allows.
            if (source().available() <= 0) {
                break;
            }
        }
        return total;
    }

    public synchronized long skip(long n) throws IOException {
        buffer();
        if (n <= 0) {
            return 0;
        }
        final long avail = count - pos;
        if (avail <= 0) {
            if (markpos < 0) {
                return source().skip(n);
            }
            fill();
            if (count - pos <= 0) {
                return 0;
            }
        }
        final long skipped = Math.min(count - pos, n);
        pos += (int) skipped;
        return skipped;
    }

    public synchronized int available() throws IOException {
        final int local = count - pos;
        final int remote = source().available();
        return local > Integer.MAX_VALUE - remote ? Integer.MAX_VALUE : local + remote;
    }

    public synchronized void mark(int readlimit) {
        marklimit = readlimit;
        markpos = pos;
    }

    public synchronized void reset() throws IOException {
        buffer();
        if (markpos < 0) {
            throw new IOException("Resetting to invalid mark");
        }
        pos = markpos;
    }

    public boolean markSupported() {
        return true;
    }

    public void close() throws IOException {
        buf = null;
        final InputStream s = in;
        in = null;
        if (s != null) {
            s.close();
        }
    }
}
