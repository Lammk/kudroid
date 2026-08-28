package android.os;

import java.io.Closeable;
import java.io.FileDescriptor;
import java.io.IOException;

public class ParcelFileDescriptor implements Parcelable, Closeable {
    public static final int MODE_READ_ONLY = 0x10000000;
    public static final int MODE_WRITE_ONLY = 0x20000000;
    public static final int MODE_READ_WRITE = 0x30000000;
    public static final int MODE_CREATE = 0x08000000;

    private final FileDescriptor fd;

    public ParcelFileDescriptor() { this.fd = new FileDescriptor(); }
    public ParcelFileDescriptor(FileDescriptor fd) { this.fd = fd; }
    public FileDescriptor getFileDescriptor() { return fd; }
    public int getFd() { return -1; }
    public void close() throws IOException {}
    public int describeContents() { return 0; }
    public void writeToParcel(Parcel out, int flags) {}
}
