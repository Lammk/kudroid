package java.io;

import java.nio.ByteBuffer;
import java.nio.channels.ClosedChannelException;
import java.nio.channels.FileChannel;
import java.nio.channels.NonReadableChannelException;
import java.nio.channels.NonWritableChannelException;
import java.nio.channels.ReadableByteChannel;
import java.nio.channels.WritableByteChannel;

/** The channel of a FileInputStream or FileOutputStream; reads and writes go through the stream. */
final class StreamFileChannel extends FileChannel {
    private final FileInputStream in;
    private final FileOutputStream out;
    private final String path;
    private long position;
    private boolean open = true;

    StreamFileChannel(FileInputStream in, FileOutputStream out, String path) {
        this.in = in;
        this.out = out;
        this.path = path;
    }

    @Override protected Object lockKey() {
        return path != null ? (Object) new File(path).getAbsolutePath() : this;
    }

    private void ensureOpen() throws IOException {
        if (!open) throw new ClosedChannelException();
    }

    @Override public boolean isOpen() {
        return open;
    }

    @Override public int read(ByteBuffer dst) throws IOException {
        ensureOpen();
        if (in == null) throw new NonReadableChannelException();
        final int want = dst.remaining();
        if (want == 0) return 0;
        final byte[] tmp = new byte[Math.min(want, 65536)];
        final int n = in.read(tmp, 0, tmp.length);
        if (n > 0) {
            dst.put(tmp, 0, n);
            position += n;
        }
        return n;
    }

    @Override public long read(ByteBuffer[] dsts, int offset, int length) throws IOException {
        long total = 0;
        for (int i = offset; i < offset + length; i++) {
            if (!dsts[i].hasRemaining()) continue;
            final int n = read(dsts[i]);
            if (n < 0) return total == 0 ? -1 : total;
            total += n;
            if (dsts[i].hasRemaining()) break;
        }
        return total;
    }

    @Override public int write(ByteBuffer src) throws IOException {
        ensureOpen();
        if (out == null) throw new NonWritableChannelException();
        final int n = src.remaining();
        final byte[] tmp = new byte[n];
        src.get(tmp, 0, n);
        out.write(tmp, 0, n);
        position += n;
        return n;
    }

    @Override public long write(ByteBuffer[] srcs, int offset, int length) throws IOException {
        long total = 0;
        for (int i = offset; i < offset + length; i++) total += write(srcs[i]);
        return total;
    }

    @Override public long position() throws IOException {
        ensureOpen();
        return position;
    }

    /** Streams only move forward, so only a forward move of an input channel is honoured. */
    @Override public FileChannel position(long newPosition) throws IOException {
        ensureOpen();
        if (newPosition < 0) throw new IllegalArgumentException();
        if (newPosition == position) return this;
        if (in == null || newPosition < position) {
            throw new IOException("Channel position cannot move " + (in == null ? "on a write-only" : "backwards on a")
                    + " stream channel");
        }
        final long skipped = in.skip(newPosition - position);
        position += Math.max(0L, skipped);
        return this;
    }

    @Override public long size() throws IOException {
        ensureOpen();
        return path != null ? new File(path).length() : position;
    }

    @Override public FileChannel truncate(long size) throws IOException {
        ensureOpen();
        if (out == null) throw new NonWritableChannelException();
        if (path != null && size < new File(path).length()) {
            final RandomAccessFile raf = new RandomAccessFile(path, "rw");
            try {
                raf.setLength(size);
            } finally {
                raf.close();
            }
        }
        return this;
    }

    @Override public void force(boolean metaData) throws IOException {
        ensureOpen();
        if (out != null) out.flush();
    }

    @Override public long transferTo(long position, long count, WritableByteChannel target) throws IOException {
        ensureOpen();
        if (in == null) throw new NonReadableChannelException();
        if (position != this.position) position(position);
        long done = 0;
        final byte[] tmp = new byte[8192];
        while (done < count) {
            final int n = in.read(tmp, 0, (int) Math.min(tmp.length, count - done));
            if (n <= 0) break;
            this.position += n;
            target.write(ByteBuffer.wrap(tmp, 0, n));
            done += n;
        }
        return done;
    }

    @Override public long transferFrom(ReadableByteChannel src, long position, long count) throws IOException {
        ensureOpen();
        if (out == null) throw new NonWritableChannelException();
        long done = 0;
        final ByteBuffer tmp = ByteBuffer.allocate(8192);
        while (done < count) {
            tmp.clear();
            tmp.limit((int) Math.min(tmp.capacity(), count - done));
            final int n = src.read(tmp);
            if (n <= 0) break;
            out.write(tmp.array(), 0, n);
            this.position += n;
            done += n;
        }
        return done;
    }

    @Override public void close() throws IOException {
        if (!open) return;
        open = false;
        super.close();
        if (in != null) in.close();
        if (out != null) out.close();
    }
}
