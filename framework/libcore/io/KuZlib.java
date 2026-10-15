package libcore.io;

/**
 * zlib streams and checksums behind java.util.zip, implemented by the runtime.
 *
 * inflate/deflate take the buffers on every call and report how many bytes they consumed and
 * produced in counts[0] and counts[1]. Status: 0 progress, 1 end of stream, 2 dictionary
 * needed, negative for an error (-3 data error, -4 out of memory).
 */
public final class KuZlib {
    private KuZlib() {}

    public static final int NO_FLUSH = 0;
    public static final int SYNC_FLUSH = 2;
    public static final int FULL_FLUSH = 3;
    public static final int FINISH = 4;

    public static native long inflateInit(boolean nowrap);
    public static native long deflateInit(int level, boolean nowrap, int strategy);
    public static native int inflate(long handle, byte[] in, int inOff, int inLen,
            byte[] out, int outOff, int outLen, int unused, int[] counts);
    public static native int deflate(long handle, byte[] in, int inOff, int inLen,
            byte[] out, int outOff, int outLen, int flush, int[] counts);
    public static native int inflateReset(long handle);
    public static native int deflateReset(long handle);
    public static native void inflateEnd(long handle);
    public static native void deflateEnd(long handle);
    public static native int crc32(int crc, byte[] buf, int off, int len);
    public static native int adler32(int adler, byte[] buf, int off, int len);
}
