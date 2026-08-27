package java.io;

public class FileInputStream extends InputStream {

    /** fd của POSIX; -1 = đã đóng. */
    private int fd = -1;

    public FileInputStream(String name) throws FileNotFoundException {
        fd = openNative(name);
        if (fd < 0) {
            throw new FileNotFoundException(name);
        }
    }

    public FileInputStream(File file) throws FileNotFoundException {
        this(file.getPath());
    }

    public int read() throws IOException {
        byte[] one = new byte[1];
        int n = read(one, 0, 1);
        return n <= 0 ? -1 : (one[0] & 0xff);
    }

    public int read(byte[] b, int off, int len) throws IOException {
        if (fd < 0) {
            throw new IOException("stream đã đóng");
        }
        return readNative(fd, b, off, len);
    }

    public long skip(long n) throws IOException {
        if (fd < 0) {
            throw new IOException("stream đã đóng");
        }
        return skipNative(fd, n);
    }

    public int available() throws IOException {
        return fd < 0 ? 0 : availableNative(fd);
    }

    public void close() throws IOException {
        if (fd >= 0) {
            closeNative(fd);
            fd = -1;
        }
    }

    private static native int openNative(String path);

    private static native int readNative(int fd, byte[] b, int off, int len);

    private static native long skipNative(int fd, long n);

    private static native int availableNative(int fd);

    private static native void closeNative(int fd);
}
