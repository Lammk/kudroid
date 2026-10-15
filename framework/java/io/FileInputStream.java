package java.io;

/**
 * java.io.FileInputStream, reading through the POSIX natives in LibCore.cpp.
 *
 * Every method here used to return -1, which is indistinguishable from an empty file: a
 * caller reading its own data back got nothing and no error. The natives existed the whole
 * time — openNative/readNative/skipNative/availableNative/closeNative in LibCore.cpp, with
 * bounds checks — but no Java class declared them, so nothing ever called them.
 *
 * Paths go through the VFS remapper on the native side, so a guest cannot reach outside
 * android_root.
 */
public class FileInputStream extends InputStream {
    private final String path;
    private final FileDescriptor fd;
    private int handle = -1;
    private boolean closed = false;

    public FileInputStream(String name) throws FileNotFoundException {
        this.path = name;
        this.fd = new FileDescriptor();
        if (name == null) {
            throw new FileNotFoundException("null path");
        }
        this.handle = openNative(name);
        if (this.handle < 0) {
            // Throwing is the contract, and it matters: a caller that gets a usable stream
            // for a missing file reads zero bytes and treats that as valid empty data.
            throw new FileNotFoundException(name + " (no such file or directory)");
        }
        this.fd.setInt$(this.handle);
    }

    public FileInputStream(File file) throws FileNotFoundException {
        this(file != null ? file.getPath() : null);
    }

    public FileInputStream(FileDescriptor fdObj) {
        this.path = null;
        this.fd = fdObj;
        this.handle = fdObj != null ? fdObj.getInt$() : -1;
    }

    public int read() throws IOException {
        byte[] one = new byte[1];
        final int n = read(one, 0, 1);
        if (n <= 0) {
            return -1;
        }
        // A byte is signed in Java but read() returns 0..255; without the mask a byte of
        // 0xFF would be returned as -1 and read as end-of-file.
        return one[0] & 0xFF;
    }

    public int read(byte[] b, int off, int len) throws IOException {
        if (b == null) {
            throw new IOException("null buffer");
        }
        if (off < 0 || len < 0 || off + len > b.length) {
            throw new IOException("read out of bounds");
        }
        if (len == 0) {
            return 0;
        }
        if (handle < 0) {
            return -1;
        }
        final int n = readNative(handle, b, off, len);
        // read() returning 0 at end of file is what the POSIX layer reports; Java wants -1.
        if (n <= 0) {
            return -1;
        }
        return n;
    }

    public long skip(long n) throws IOException {
        if (handle < 0 || n <= 0) {
            return 0;
        }
        return skipNative(handle, n);
    }

    public int available() throws IOException {
        if (handle < 0) {
            return 0;
        }
        return availableNative(handle);
    }

    public final FileDescriptor getFD() throws IOException {
        return fd;
    }

    public void close() throws IOException {
        // Guarded: close() is routinely called twice (explicitly and from a finally block),
        // and closing a descriptor twice can close an unrelated file that reused the number.
        if (closed || handle < 0) {
            return;
        }
        closed = true;
        closeNative(handle);
        handle = -1;
    }

    private static native int openNative(String path);
    private static native int readNative(int fd, byte[] buf, int off, int len);
    private static native long skipNative(int fd, long n);
    private static native int availableNative(int fd);
    private static native void closeNative(int fd);
}
