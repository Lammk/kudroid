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
    public void close() throws IOException {}
}
