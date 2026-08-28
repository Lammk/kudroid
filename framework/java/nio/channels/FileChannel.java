package java.nio.channels;

import java.nio.ByteBuffer;

/**
 * java.nio.channels.FileChannel — minimal abstract shape.
 *
 * Declared abstract with no implementation: FileInputStream.getChannel() and friends
 * reference the type, and code that only passes a channel around needs the type to
 * resolve. Anything that actually performs I/O through it will fail loudly with
 * NoClassDefFoundError from KuART rather than silently reading nothing.
 */
public abstract class FileChannel implements ByteChannel {
    public static class MapMode {
        public static final MapMode READ_ONLY = new MapMode("READ_ONLY");
        public static final MapMode READ_WRITE = new MapMode("READ_WRITE");
        public static final MapMode PRIVATE = new MapMode("PRIVATE");

        private final String name;

        private MapMode(String name) {
            this.name = name;
        }

        @Override
        public String toString() {
            return name;
        }
    }

    public abstract int read(ByteBuffer dst) throws java.io.IOException;

    public abstract int write(ByteBuffer src) throws java.io.IOException;

    public abstract long position() throws java.io.IOException;

    public abstract FileChannel position(long newPosition) throws java.io.IOException;

    public abstract long size() throws java.io.IOException;
}
