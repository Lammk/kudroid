package java.nio.channels;

import java.nio.ByteBuffer;
import java.io.IOException;

public interface ReadableByteChannel extends Channel {
    int read(ByteBuffer dst) throws IOException;
}
