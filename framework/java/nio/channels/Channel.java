package java.nio.channels;

import java.io.Closeable;

/**
 * java.nio.channels.Channel — base interface for anything that can be open or closed.
 */
public interface Channel extends Closeable {
    boolean isOpen();

    void close() throws java.io.IOException;
}
