package java.nio.channels;

import java.nio.ByteBuffer;

/**
 * java.nio.channels.ReadableByteChannel — a channel that can be read into a buffer.
 */
public interface ReadableByteChannel extends Channel {
    int read(ByteBuffer dst) throws java.io.IOException;
}
