package java.io;

public class FileOutputStream extends OutputStream {

    private int fd = -1;

    public FileOutputStream(String name) throws FileNotFoundException {
        this(name, false);
    }

    public FileOutputStream(String name, boolean append) throws FileNotFoundException {
        fd = openNative(name, append);
        if (fd < 0) {
            throw new FileNotFoundException(name);
        }
    }

    public FileOutputStream(File file) throws FileNotFoundException {
        this(file.getPath(), false);
    }

    public FileOutputStream(File file, boolean append) throws FileNotFoundException {
        this(file.getPath(), append);
    }

    public void write(int b) throws IOException {
        write(new byte[] { (byte) b }, 0, 1);
    }

    public void write(byte[] b, int off, int len) throws IOException {
        if (fd < 0) {
            throw new IOException("stream đã đóng");
        }
        if (writeNative(fd, b, off, len) < 0) {
            throw new IOException("ghi thất bại");
        }
    }

    public void close() throws IOException {
        if (fd >= 0) {
            closeNative(fd);
            fd = -1;
        }
    }

    private static native int openNative(String path, boolean append);

    private static native int writeNative(int fd, byte[] b, int off, int len);

    private static native void closeNative(int fd);
}
