package java.io;

public final class FileDescriptor {
    private int descriptor = -1;
    public static final FileDescriptor in = new FileDescriptor(0);
    public static final FileDescriptor out = new FileDescriptor(1);
    public static final FileDescriptor err = new FileDescriptor(2);

    public FileDescriptor() {}
    private FileDescriptor(int descriptor) { this.descriptor = descriptor; }
    public boolean valid() { return descriptor != -1; }
    public void sync() throws SyncFailedException {}

    /**
     * The underlying descriptor number.
     *
     * Public with the same names libcore uses. The file streams publish the descriptor
     * they opened, and android.os.ParcelFileDescriptor (a different package) needs to
     * read it back for getFd()/detachFd() — package-private made that impossible and
     * left getFd() returning -1 to every native caller.
     */
    public int getInt$() { return descriptor; }
    public void setInt$(int fd) { this.descriptor = fd; }
}
