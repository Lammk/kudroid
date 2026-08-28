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
    public FileInputStream createInputStream() throws IOException {
        return new FileInputStream(getFileDescriptor());
    }
    public FileOutputStream createOutputStream() throws IOException {
        return new FileOutputStream(getFileDescriptor());
    }
    public void close() throws IOException {
        if (mFd != null) mFd.close();
    }
    public int describeContents() { return 0; }
    public void writeToParcel(Parcel out, int flags) {}
}
