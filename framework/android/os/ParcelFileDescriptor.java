package android.os;

import java.io.Closeable;
import java.io.FileDescriptor;
import java.io.IOException;

/**
 * android.os.ParcelFileDescriptor.
 *
 * Backed by a real java.io.FileDescriptor number so native consumers that ask for a raw
 * fd (MediaExtractor.setDataSourceFd, Unity's AndroidVideoMedia, FMOD's file open) get
 * one. getFd() used to return -1 unconditionally and detachFd()/adoptFd() did not exist
 * (auto-stubbed to 0), which handed every fd-based native the wrong descriptor.
 */
public class ParcelFileDescriptor implements Parcelable, Closeable {
    public static final int MODE_READ_ONLY = 0x10000000;
    public static final int MODE_WRITE_ONLY = 0x20000000;
    public static final int MODE_READ_WRITE = 0x30000000;
    public static final int MODE_CREATE = 0x08000000;

    private final FileDescriptor fd;

    public ParcelFileDescriptor() { this.fd = new FileDescriptor(); }
    public ParcelFileDescriptor(FileDescriptor fd) { this.fd = fd; }
    public FileDescriptor getFileDescriptor() { return fd; }

    /** The raw descriptor number, or -1 when this wrapper owns no valid descriptor. */
    public int getFd() { return fd != null ? fd.getInt$() : -1; }

    public void close() throws IOException {}

    /**
     * Transfer ownership of the raw descriptor to the caller. After detach this wrapper
     * reports no descriptor, so a later close() cannot close a descriptor a native
     * consumer now owns.
     */
    public int detachFd() {
        if (fd == null) return -1;
        final int raw = fd.getInt$();
        if (raw >= 0) fd.setInt$(-1);
        return raw;
    }

    /** Wrap an already-open raw descriptor, taking ownership of it. */
    public static ParcelFileDescriptor adoptFd(int fd) {
        if (fd < 0) return null;
        final FileDescriptor descriptor = new FileDescriptor();
        descriptor.setInt$(fd);
        return new ParcelFileDescriptor(descriptor);
    }

    public int describeContents() { return 0; }
    public void writeToParcel(Parcel out, int flags) {}
}
