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
     * Package-private with the same names libcore uses, so the file streams can publish the
     * descriptor they opened. Without it getFD() returns a FileDescriptor whose valid() is
     * false even for an open file, and code that checks valid() before reading gives up.
     */
    int getInt$() { return descriptor; }
    void setInt$(int fd) { this.descriptor = fd; }
}
