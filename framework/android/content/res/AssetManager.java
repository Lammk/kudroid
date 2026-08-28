package android.content.res;

import java.io.InputStream;
import java.io.ByteArrayInputStream;
import java.io.IOException;

public final class AssetManager implements AutoCloseable {
    public AssetManager() {}
    public InputStream open(String fileName) throws IOException {
        return new ByteArrayInputStream(new byte[0]);
    }
    public InputStream open(String fileName, int accessMode) throws IOException {
        return open(fileName);
    }
    public AssetFileDescriptor openFd(String fileName) throws IOException {
        return null;
    }
    public String[] list(String path) throws IOException {
        return new String[0];
    }
    public String[] getLocales() {
        return new String[]{"en_US"};
    }
    public void close() {}
}
