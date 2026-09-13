package android.content.res;

import android.os.Parcel;
import android.os.Parcelable;
import android.os.ParcelFileDescriptor;
import java.io.Closeable;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.FileDescriptor;
import java.io.IOException;

public class AssetFileDescriptor implements Parcelable, Closeable {
    public static final long UNKNOWN_LENGTH = -1;
    private final ParcelFileDescriptor mFd;
    private final long mStartOffset;
    private final long mLength;

    public AssetFileDescriptor(ParcelFileDescriptor fd, long startOffset, long length) {
        mFd = fd;
        mStartOffset = startOffset;
        mLength = length;
    }
    public ParcelFileDescriptor getParcelFileDescriptor() { return mFd; }
    public FileDescriptor getFileDescriptor() { return mFd != null ? mFd.getFileDescriptor() : null; }
    public long getStartOffset() { return mStartOffset; }
    public long getLength() { return mLength; }
    public long getDeclaredLength() { return mLength; }

    /**
     * A stream over exactly this entry's bytes. Android seeks to mStartOffset and caps
     * reads at mLength; returning a raw stream from byte 0 hands the caller the ZIP
     * local header (PK\x03\x04) instead of the payload, which is what made FMOD/asset
     * readers report a corrupt file. The return type must stay FileInputStream for
     * signature compatibility, hence the subclass rather than a FilterInputStream.
     */
    public FileInputStream createInputStream() throws IOException {
        return new BoundedFileInputStream(getFileDescriptor(), mStartOffset, mLength);
    }

    public FileOutputStream createOutputStream() throws IOException {
        return new FileOutputStream(getFileDescriptor());
    }

    public void close() throws IOException {
        if (mFd != null) mFd.close();
    }

    public int describeContents() { return 0; }
    public void writeToParcel(Parcel out, int flags) {}

    /** A FileInputStream positioned at an entry payload and capped at its length. */
    public static class BoundedFileInputStream extends FileInputStream {
        private long mRemaining;

        public BoundedFileInputStream(FileDescriptor fd, long startOffset, long length)
                throws IOException {
            super(fd);
            long remaining = startOffset;
            while (remaining > 0) {
                final long skipped = super.skip(remaining);
                if (skipped <= 0) break;
                remaining -= skipped;
            }
            if (remaining > 0) {
                throw new IOException("asset start offset " + startOffset
                        + " is past end of backing file");
            }
            mRemaining = length < 0 ? Long.MAX_VALUE : length;
        }

        @Override
        public int read() throws IOException {
            if (mRemaining <= 0) return -1;
            // super.read(byte[],int,int) is the non-virtual bound-checking entry point;
            // calling this.read() would dispatch back here and double-count mRemaining.
            final byte[] one = new byte[1];
            final int n = super.read(one, 0, 1);
            if (n <= 0) return -1;
            mRemaining -= n;
            return one[0] & 0xFF;
        }

        @Override
        public int read(byte[] b, int off, int len) throws IOException {
            if (mRemaining <= 0) return -1;
            final int n = super.read(b, off, (int) Math.min((long) len, mRemaining));
            if (n > 0) mRemaining -= n;
            return n;
        }

        @Override
        public long skip(long n) throws IOException {
            if (mRemaining <= 0) return 0;
            final long k = super.skip(Math.min(n, mRemaining));
            mRemaining -= k;
            return k;
        }

        @Override
        public int available() throws IOException {
            return (int) Math.min((long) super.available(), mRemaining);
        }
    }
}
