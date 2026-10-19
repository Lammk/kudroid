package java.io;

import java.nio.ByteBuffer;
import java.nio.channels.FileChannel;
import java.nio.channels.ReadableByteChannel;
import java.nio.channels.WritableByteChannel;

/**
 * java.io.RandomAccessFile over the POSIX natives in LibCore.cpp.
 *
 * Paths go through the VFS remapper on the native side, like the other file streams.
 */
public class RandomAccessFile implements DataOutput, DataInput, Closeable {
    private final FileDescriptor fd = new FileDescriptor();
    private final String path;
    private final boolean writable;
    private final boolean sync;
    private int handle = -1;
    private FileChannel channel;

    public RandomAccessFile(String name, String mode) throws FileNotFoundException {
        this(name != null ? new File(name) : null, mode);
    }

    public RandomAccessFile(File file, String mode) throws FileNotFoundException {
        if (mode == null) {
            throw new IllegalArgumentException("mode == null");
        }
        final boolean rw;
        switch (mode) {
            case "r":
                rw = false;
                sync = false;
                break;
            case "rw":
                rw = true;
                sync = false;
                break;
            case "rws":
            case "rwd":
                rw = true;
                sync = true;
                break;
            default:
                throw new IllegalArgumentException("Illegal mode \"" + mode
                        + "\" must be one of \"r\", \"rw\", \"rws\", or \"rwd\"");
        }
        if (file == null) {
            throw new NullPointerException("file == null");
        }
        writable = rw;
        path = file.getPath();
        final int h = openNative(path, rw);
        if (h < 0) {
            throw new FileNotFoundException(path + " (" + openError(h) + ")");
        }
        handle = h;
        fd.setInt$(h);
    }

    private static String openError(int code) {
        switch (code) {
            case -2: return "No such file or directory";
            case -3: return "Permission denied";
            case -4: return "Is a directory";
            default: return "Cannot open file";
        }
    }

    private int open() throws IOException {
        if (handle < 0) {
            throw new IOException("Stream Closed");
        }
        return handle;
    }

    public final FileDescriptor getFD() throws IOException {
        return fd;
    }

    public final synchronized FileChannel getChannel() {
        if (channel == null) {
            channel = new RafChannel();
        }
        return channel;
    }

    public int read() throws IOException {
        final byte[] one = new byte[1];
        return read(one, 0, 1) == 1 ? one[0] & 0xff : -1;
    }

    public int read(byte[] b, int off, int len) throws IOException {
        if (b == null) {
            throw new NullPointerException("b == null");
        }
        if ((off | len | (off + len) | (b.length - (off + len))) < 0) {
            throw new IndexOutOfBoundsException();
        }
        final int h = open();
        if (len == 0) {
            return 0;
        }
        final int n = readNative(h, b, off, len);
        if (n < 0) {
            throw new IOException("read failed");
        }
        return n == 0 ? -1 : n;
    }

    public int read(byte[] b) throws IOException {
        return read(b, 0, b.length);
    }

    public final void readFully(byte[] b) throws IOException {
        readFully(b, 0, b.length);
    }

    public final void readFully(byte[] b, int off, int len) throws IOException {
        int done = 0;
        while (done < len) {
            final int n = read(b, off + done, len - done);
            if (n < 0) {
                throw new EOFException();
            }
            done += n;
        }
    }

    public int skipBytes(int n) throws IOException {
        if (n <= 0) {
            return 0;
        }
        final long pos = getFilePointer();
        final long end = length();
        final long target = Math.min(pos + n, end);
        seek(target);
        return (int) (target - pos);
    }

    public void write(int b) throws IOException {
        write(new byte[] {(byte) b}, 0, 1);
    }

    public void write(byte[] b) throws IOException {
        write(b, 0, b.length);
    }

    public void write(byte[] b, int off, int len) throws IOException {
        if (b == null) {
            throw new NullPointerException("b == null");
        }
        if ((off | len | (off + len) | (b.length - (off + len))) < 0) {
            throw new IndexOutOfBoundsException();
        }
        final int h = open();
        if (!writable) {
            throw new IOException("Bad file descriptor");
        }
        int done = 0;
        while (done < len) {
            final int n = writeNative(h, b, off + done, len - done);
            if (n <= 0) {
                throw new IOException("write failed");
            }
            done += n;
        }
        if (sync) {
            syncNative(h);
        }
    }

    public long getFilePointer() throws IOException {
        final long pos = positionNative(open());
        if (pos < 0) {
            throw new IOException("Cannot get file pointer");
        }
        return pos;
    }

    public void seek(long pos) throws IOException {
        if (pos < 0) {
            throw new IOException("offset < 0: " + pos);
        }
        if (seekNative(open(), pos) < 0) {
            throw new IOException("Cannot seek to " + pos);
        }
    }

    public long length() throws IOException {
        final long n = lengthNative(open());
        if (n < 0) {
            throw new IOException("Cannot get file length");
        }
        return n;
    }

    public void setLength(long newLength) throws IOException {
        if (newLength < 0) {
            throw new IllegalArgumentException("newLength < 0");
        }
        final int h = open();
        if (!writable) {
            throw new IOException("Bad file descriptor");
        }
        if (setLengthNative(h, newLength) < 0) {
            throw new IOException("Cannot set file length");
        }
        // The pointer never stays beyond the new end.
        if (getFilePointer() > newLength) {
            seek(newLength);
        }
    }

    public void close() throws IOException {
        final int h = handle;
        if (h < 0) {
            return;
        }
        handle = -1;
        fd.setInt$(-1);
        closeNative(h);
    }

    public final boolean readBoolean() throws IOException {
        return readUnsignedByte() != 0;
    }

    public final byte readByte() throws IOException {
        return (byte) readUnsignedByte();
    }

    public final int readUnsignedByte() throws IOException {
        final int c = read();
        if (c < 0) {
            throw new EOFException();
        }
        return c;
    }

    public final short readShort() throws IOException {
        return (short) readUnsignedShort();
    }

    public final int readUnsignedShort() throws IOException {
        final byte[] w = new byte[2];
        readFully(w, 0, 2);
        return ((w[0] & 0xff) << 8) | (w[1] & 0xff);
    }

    public final char readChar() throws IOException {
        return (char) readUnsignedShort();
    }

    public final int readInt() throws IOException {
        final byte[] w = new byte[4];
        readFully(w, 0, 4);
        return ((w[0] & 0xff) << 24) | ((w[1] & 0xff) << 16) | ((w[2] & 0xff) << 8) | (w[3] & 0xff);
    }

    public final long readLong() throws IOException {
        final byte[] w = new byte[8];
        readFully(w, 0, 8);
        long v = 0;
        for (int i = 0; i < 8; i++) {
            v = (v << 8) | (w[i] & 0xffL);
        }
        return v;
    }

    public final float readFloat() throws IOException {
        return Float.intBitsToFloat(readInt());
    }

    public final double readDouble() throws IOException {
        return Double.longBitsToDouble(readLong());
    }

    public final String readLine() throws IOException {
        final StringBuilder sb = new StringBuilder();
        boolean any = false;
        while (true) {
            final int c = read();
            if (c < 0) {
                return any ? sb.toString() : null;
            }
            any = true;
            if (c == '\n') {
                return sb.toString();
            }
            if (c == '\r') {
                final long pos = getFilePointer();
                if (read() != '\n') {
                    seek(pos);
                }
                return sb.toString();
            }
            sb.append((char) c);
        }
    }

    public final String readUTF() throws IOException {
        return DataInputStream.readUTF(this);
    }

    public final void writeBoolean(boolean v) throws IOException {
        write(v ? 1 : 0);
    }

    public final void writeByte(int v) throws IOException {
        write(v);
    }

    public final void writeShort(int v) throws IOException {
        write(new byte[] {(byte) (v >>> 8), (byte) v}, 0, 2);
    }

    public final void writeChar(int v) throws IOException {
        writeShort(v);
    }

    public final void writeInt(int v) throws IOException {
        write(new byte[] {(byte) (v >>> 24), (byte) (v >>> 16), (byte) (v >>> 8), (byte) v}, 0, 4);
    }

    public final void writeLong(long v) throws IOException {
        final byte[] w = new byte[8];
        for (int i = 0; i < 8; i++) {
            w[i] = (byte) (v >>> (56 - 8 * i));
        }
        write(w, 0, 8);
    }

    public final void writeFloat(float v) throws IOException {
        writeInt(Float.floatToIntBits(v));
    }

    public final void writeDouble(double v) throws IOException {
        writeLong(Double.doubleToLongBits(v));
    }

    public final void writeBytes(String s) throws IOException {
        final byte[] b = new byte[s.length()];
        for (int i = 0; i < b.length; i++) {
            b[i] = (byte) s.charAt(i);
        }
        write(b, 0, b.length);
    }

    public final void writeChars(String s) throws IOException {
        final byte[] b = new byte[s.length() * 2];
        for (int i = 0; i < s.length(); i++) {
            final char c = s.charAt(i);
            b[2 * i] = (byte) (c >>> 8);
            b[2 * i + 1] = (byte) c;
        }
        write(b, 0, b.length);
    }

    public final void writeUTF(String s) throws IOException {
        final byte[] encoded = DataOutputStream.encodeUTF(s);
        write(encoded, 0, encoded.length);
    }

    /** Channel view sharing this file's descriptor and position. */
    private final class RafChannel extends FileChannel {
        public int read(ByteBuffer dst) throws IOException {
            final int want = dst.remaining();
            if (want == 0) {
                return 0;
            }
            final byte[] tmp = new byte[Math.min(want, 65536)];
            final int n = RandomAccessFile.this.read(tmp, 0, tmp.length);
            if (n > 0) {
                dst.put(tmp, 0, n);
            }
            return n;
        }

        public long read(ByteBuffer[] dsts, int offset, int length) throws IOException {
            long total = 0;
            for (int i = offset; i < offset + length; i++) {
                if (!dsts[i].hasRemaining()) {
                    continue;
                }
                final int n = read(dsts[i]);
                if (n < 0) {
                    return total == 0 ? -1 : total;
                }
                total += n;
                if (dsts[i].hasRemaining()) {
                    break;
                }
            }
            return total;
        }

        public int write(ByteBuffer src) throws IOException {
            final int n = src.remaining();
            final byte[] tmp = new byte[n];
            src.get(tmp, 0, n);
            RandomAccessFile.this.write(tmp, 0, n);
            return n;
        }

        public long write(ByteBuffer[] srcs, int offset, int length) throws IOException {
            long total = 0;
            for (int i = offset; i < offset + length; i++) {
                total += write(srcs[i]);
            }
            return total;
        }

        public long position() throws IOException {
            return getFilePointer();
        }

        public FileChannel position(long newPosition) throws IOException {
            seek(newPosition);
            return this;
        }

        public long size() throws IOException {
            return length();
        }

        public FileChannel truncate(long size) throws IOException {
            if (size < length()) {
                final long pos = getFilePointer();
                setLength(size);
                if (pos < size) {
                    seek(pos);
                }
            }
            return this;
        }

        public void force(boolean metaData) throws IOException {
            syncNative(open());
        }

        public long transferTo(long position, long count, WritableByteChannel target) throws IOException {
            final long saved = getFilePointer();
            try {
                seek(position);
                long done = 0;
                final byte[] tmp = new byte[8192];
                while (done < count) {
                    final int n = RandomAccessFile.this.read(tmp, 0, (int) Math.min(tmp.length, count - done));
                    if (n <= 0) {
                        break;
                    }
                    target.write(ByteBuffer.wrap(tmp, 0, n));
                    done += n;
                }
                return done;
            } finally {
                seek(saved);
            }
        }

        public long transferFrom(ReadableByteChannel src, long position, long count) throws IOException {
            final long saved = getFilePointer();
            try {
                seek(position);
                long done = 0;
                final ByteBuffer tmp = ByteBuffer.allocate(8192);
                while (done < count) {
                    tmp.clear();
                    tmp.limit((int) Math.min(tmp.capacity(), count - done));
                    final int n = src.read(tmp);
                    if (n <= 0) {
                        break;
                    }
                    RandomAccessFile.this.write(tmp.array(), 0, n);
                    done += n;
                }
                return done;
            } finally {
                seek(saved);
            }
        }

        public boolean isOpen() {
            return handle >= 0;
        }

        public void close() throws IOException {
            RandomAccessFile.this.close();
        }
    }

    private static native int openNative(String path, boolean write);
    private static native int readNative(int fd, byte[] buf, int off, int len);
    private static native int writeNative(int fd, byte[] buf, int off, int len);
    private static native long seekNative(int fd, long pos);
    private static native long positionNative(int fd);
    private static native long lengthNative(int fd);
    private static native int setLengthNative(int fd, long length);
    private static native void syncNative(int fd);
    private static native void closeNative(int fd);
}
