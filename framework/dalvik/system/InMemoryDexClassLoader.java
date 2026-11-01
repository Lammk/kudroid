package dalvik.system;

import java.io.IOException;
import java.nio.ByteBuffer;

public final class InMemoryDexClassLoader extends BaseDexClassLoader {
    private final String loadError;
    private static native String registerDexBuffers(byte[][] buffers);

    public InMemoryDexClassLoader(ByteBuffer buffer, ClassLoader parent) {
        this(new ByteBuffer[] {buffer}, null, parent);
    }

    public InMemoryDexClassLoader(ByteBuffer[] buffers, ClassLoader parent) {
        this(buffers, null, parent);
    }

    public InMemoryDexClassLoader(ByteBuffer[] buffers, String librarySearchPath,
                                  ClassLoader parent) {
        super("", null, librarySearchPath, parent);
        if (buffers == null) throw new NullPointerException("buffers == null");
        for (ByteBuffer buffer : buffers) {
            if (buffer == null) throw new NullPointerException("null DEX buffer");
        }
        byte[][] snapshots = new byte[buffers.length][];
        for (int i = 0; i < buffers.length; i++) {
            ByteBuffer buffer = buffers[i];
            int start = buffer.position();
            byte[] bytes = new byte[buffer.remaining()];
            for (int j = 0; j < bytes.length; j++) bytes[j] = buffer.get(start + j);
            snapshots[i] = bytes;
        }
        loadError = registerDexBuffers(snapshots);
    }

    @Override
    public Class<?> loadClass(String name) throws ClassNotFoundException {
        try {
            return super.loadClass(name);
        } catch (ClassNotFoundException exception) {
            if (loadError != null) exception.addSuppressed(new IOException(loadError));
            throw exception;
        }
    }
}
