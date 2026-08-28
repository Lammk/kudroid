package java.util.zip;

import java.io.File;
import java.io.InputStream;
import java.io.IOException;
import java.io.Closeable;
import java.util.Enumeration;
import java.util.Collections;

public class ZipFile implements Closeable {
    public static final int OPEN_READ = 0x1;
    public static final int OPEN_DELETE = 0x4;

    private final String name;

    public ZipFile(String name) throws IOException {
        this(new File(name), OPEN_READ);
    }
    public ZipFile(File file, int mode) throws IOException {
        if (file == null) throw new NullPointerException();
        this.name = file.getPath();
    }
    public ZipFile(File file) throws ZipException, IOException {
        this(file, OPEN_READ);
    }

    public ZipEntry getEntry(String name) {
        return null;
    }
    public InputStream getInputStream(ZipEntry entry) throws IOException {
        return null;
    }
    public String getName() { return name; }
    public Enumeration<? extends ZipEntry> entries() {
        return Collections.<ZipEntry>emptyEnumeration();
    }
    public int size() { return 0; }
    public void close() throws IOException {}
}
