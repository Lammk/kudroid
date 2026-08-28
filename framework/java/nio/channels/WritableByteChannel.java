package java.nio.channels;

import java.nio.ByteBuffer;

/**
 * java.nio.channels.WritableByteChannel — a channel that can be written from a buffer.
 */
public interface WritableByteChannel extends Channel {
    int write(ByteBuffer src) throws java.io.IOException;
}
