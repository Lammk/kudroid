package java.nio.channels;

import java.io.IOException;

/** A lock held on a region of a file through a {@link FileChannel}. */
public abstract class FileLock implements AutoCloseable {
    private final FileChannel channel;
    private final long position;
    private final long size;
    private final boolean shared;

    protected FileLock(FileChannel channel, long position, long size, boolean shared) {
        if (position < 0) throw new IllegalArgumentException("Negative position");
        if (size < 0) throw new IllegalArgumentException("Negative size");
        if (position + size < 0) throw new IllegalArgumentException("Negative position + size");
        this.channel = channel;
        this.position = position;
        this.size = size;
        this.shared = shared;
    }

    public final FileChannel channel() {
        return channel;
    }

    public Channel acquiredBy() {
        return channel;
    }

    public final long position() {
        return position;
    }

    public final long size() {
        return size;
    }

    public final boolean isShared() {
        return shared;
    }

    public final boolean overlaps(long position, long size) {
        if (position + size <= this.position) return false;
        return this.position + this.size > position;
    }

    public abstract boolean isValid();

    public abstract void release() throws IOException;

    public final void close() throws IOException {
        release();
    }

    public final String toString() {
        return getClass().getName() + "[" + position + ":" + size + " " + (shared ? "shared" : "exclusive")
                + " " + (isValid() ? "valid" : "invalid") + "]";
    }
}
