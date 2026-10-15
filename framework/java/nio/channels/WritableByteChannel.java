package java.nio.channels;

import java.nio.ByteBuffer;
import java.io.IOException;

public interface WritableByteChannel extends Channel {
    int write(ByteBuffer src) throws IOException;
}
