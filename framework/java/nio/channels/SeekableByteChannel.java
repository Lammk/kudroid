package java.nio.channels;

import java.nio.ByteBuffer;
import java.io.IOException;

public interface SeekableByteChannel extends ByteChannel {
    int read(ByteBuffer dst) throws IOException;
    int write(ByteBuffer src) throws IOException;
    long position() throws IOException;
    SeekableByteChannel position(long newPosition) throws IOException;
    long size() throws IOException;
    SeekableByteChannel truncate(long size) throws IOException;
}
