package java.io;

/**
 * java.io.FileOutputStream, writing through the POSIX natives in LibCore.cpp.
 *
 * write() used to be an empty method. Not a stub that reported anything — a silent no-op:
 * an app wrote its data, got no exception, and the file was never created. Reading it back
 * then produced the default value, which looks like the data was never stored rather than
 * like the write was dropped.
 *
 * The natives existed already (openNative/writeNative/closeNative in LibCore.cpp); nothing
 * declared them, so nothing called them.
 */
public class FileOutputStream extends OutputStream {
    private final String path;
    private final FileDescriptor fd;
    private int handle = -1;
    private boolean closed = false;

    public FileOutputStream(String name) throws FileNotFoundException {
        this(name, false);
    }

    public FileOutputStream(String name, boolean append) throws FileNotFoundException {
        this.path = name;
        this.fd = new FileDescriptor();
        if (name == null) {
            throw new FileNotFoundException("null path");
        }
        this.handle = openNative(name, append);
        if (this.handle < 0) {
            throw new FileNotFoundException(name + " (cannot open for writing)");
        }
        this.fd.setInt$(this.handle);
    }

    public FileOutputStream(File file) throws FileNotFoundException {
        this(file != null ? file.getPath() : null, false);
    }

    public FileOutputStream(File file, boolean append) throws FileNotFoundException {
        this(file != null ? file.getPath() : null, append);
    }

    public FileOutputStream(FileDescriptor fdObj) {
        this.path = null;
        this.fd = fdObj;
        this.handle = fdObj != null ? fdObj.getInt$() : -1;
    }

    public void write(int b) throws IOException {
        final byte[] one = new byte[1];
        one[0] = (byte) b;
        write(one, 0, 1);
    }

    public void write(byte[] b, int off, int len) throws IOException {
        if (b == null) {
            throw new IOException("null buffer");
        }
        if (off < 0 || len < 0 || off + len > b.length) {
            throw new IOException("write out of bounds");
        }
        if (len == 0) {
            return;
        }
        if (handle < 0) {
            throw new IOException("stream closed");
        }
        // A short write is not an error at the POSIX level, so loop until everything is out.
        // Dropping the remainder would truncate the file with no exception — the same silent
        // failure this class was written to remove.
        int written = 0;
        while (written < len) {
            final int n = writeNative(handle, b, off + written, len - written);
            if (n <= 0) {
                throw new IOException("write failed after " + written + " of " + len + " bytes");
            }
            written += n;
        }
    }

    public final FileDescriptor getFD() throws IOException {
        return fd;
    }

    public void close() throws IOException {
        if (closed || handle < 0) {
            return;
        }
        closed = true;
        closeNative(handle);
        handle = -1;
    }

    private static native int openNative(String path, boolean append);
    private static native int writeNative(int fd, byte[] buf, int off, int len);
    private static native void closeNative(int fd);
}
