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
}
