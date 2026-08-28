package java.nio.channels;

import java.io.InputStream;
import java.io.OutputStream;
import java.io.Reader;
import java.io.Writer;
import java.nio.ByteBuffer;
import java.io.IOException;

public final class Channels {
    private Channels() {}
    public static ReadableByteChannel newChannel(final InputStream in) {
        return new ReadableByteChannel() {
            private boolean open = true;
            public int read(ByteBuffer dst) throws IOException {
                byte[] buf = new byte[dst.remaining()];
                int n = in.read(buf);
                if (n > 0) dst.put(buf, 0, n);
                return n;
            }
            public boolean isOpen() { return open; }
            public void close() throws IOException { open = false; in.close(); }
        };
    }
    public static WritableByteChannel newChannel(final OutputStream out) {
        return new WritableByteChannel() {
            private boolean open = true;
            public int write(ByteBuffer src) throws IOException {
                int len = src.remaining();
                byte[] buf = new byte[len];
                src.get(buf);
                out.write(buf);
                return len;
            }
            public boolean isOpen() { return open; }
            public void close() throws IOException { open = false; out.close(); }
        };
    }
}
