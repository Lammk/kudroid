package java.nio.channels;

import java.nio.ByteBuffer;
import java.io.IOException;

public abstract class FileChannel implements ByteChannel, SeekableByteChannel, InterruptibleChannel {
    protected FileChannel() {}
    public abstract int read(ByteBuffer dst) throws IOException;
    public abstract long read(ByteBuffer[] dsts, int offset, int length) throws IOException;
    public final long read(ByteBuffer[] dsts) throws IOException { return read(dsts, 0, dsts.length); }
    public abstract int write(ByteBuffer src) throws IOException;
    public abstract long write(ByteBuffer[] srcs, int offset, int length) throws IOException;
    public final long write(ByteBuffer[] srcs) throws IOException { return write(srcs, 0, srcs.length); }
    public abstract long position() throws IOException;
    public abstract FileChannel position(long newPosition) throws IOException;
    public abstract long size() throws IOException;
    public abstract FileChannel truncate(long size) throws IOException;
    public abstract void force(boolean metaData) throws IOException;
    public abstract long transferTo(long position, long count, WritableByteChannel target) throws IOException;
    public abstract long transferFrom(ReadableByteChannel src, long position, long count) throws IOException;
    public boolean isOpen() { return true; }
    public void close() throws IOException { releaseLocks(); }

    // File locks. KuDroid is one process, so locks are tracked per file within it: overlapping
    // requests on the same file fail the way the JDK fails them.

    private static final java.util.HashMap<Object, java.util.ArrayList<FileLock>> LOCKS =
            new java.util.HashMap<Object, java.util.ArrayList<FileLock>>();
    private final java.util.ArrayList<FileLock> mHeld = new java.util.ArrayList<FileLock>();

    /** Identity of the underlying file; channels sharing a key contend for the same locks. */
    protected Object lockKey() { return this; }

    public final FileLock lock() throws IOException { return lock(0L, Long.MAX_VALUE, false); }

    public FileLock lock(long position, long size, boolean shared) throws IOException {
        return acquireLock(position, size, shared);
    }

    public final FileLock tryLock() throws IOException { return tryLock(0L, Long.MAX_VALUE, false); }

    public FileLock tryLock(long position, long size, boolean shared) throws IOException {
        return acquireLock(position, size, shared);
    }

    private FileLock acquireLock(long position, long size, boolean shared) throws IOException {
        if (position < 0 || size < 0) throw new IllegalArgumentException();
        if (!isOpen()) throw new ClosedChannelException();
        final Object key = lockKey();
        synchronized (LOCKS) {
            java.util.ArrayList<FileLock> list = LOCKS.get(key);
            if (list == null) {
                list = new java.util.ArrayList<FileLock>();
                LOCKS.put(key, list);
            }
            for (FileLock held : list) {
                if (held.overlaps(position, size)) throw new OverlappingFileLockException();
            }
            final FileLock lock = new HeldLock(this, position, size, shared, key);
            list.add(lock);
            mHeld.add(lock);
            return lock;
        }
    }

    private void releaseLocks() {
        FileLock[] copy;
        synchronized (LOCKS) {
            copy = mHeld.toArray(new FileLock[mHeld.size()]);
        }
        for (FileLock lock : copy) {
            try {
                lock.release();
            } catch (IOException ignored) {
            }
        }
    }

    private static final class HeldLock extends FileLock {
        private final FileChannel owner;
        private final Object key;
        private boolean valid = true;

        HeldLock(FileChannel channel, long position, long size, boolean shared, Object key) {
            super(channel, position, size, shared);
            this.owner = channel;
            this.key = key;
        }

        @Override public boolean isValid() {
            synchronized (LOCKS) {
                return valid;
            }
        }

        @Override public void release() throws IOException {
            synchronized (LOCKS) {
                if (!valid) return;
                valid = false;
                final java.util.ArrayList<FileLock> list = LOCKS.get(key);
                if (list != null) {
                    list.remove(this);
                    if (list.isEmpty()) LOCKS.remove(key);
                }
                owner.mHeld.remove(this);
            }
        }
    }
}
