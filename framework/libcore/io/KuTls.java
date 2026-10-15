package libcore.io;

/**
 * TLS sessions behind javax.net.ssl, implemented by the runtime.
 *
 * A session wraps a connected socket descriptor (which the caller keeps owning). The handshake
 * does not judge the server's certificate: the chain comes back through {@link #peerChain} and
 * is checked by the caller. Results are non-negative or a negative error: a negated errno, or
 * one of the TLS codes below.
 */
public final class KuTls {
    private KuTls() {}

    public static final int HANDSHAKE_FAILED = -2000;
    public static final int PROTOCOL_ERROR = -2001;
    public static final int UNSUPPORTED = -2002;

    public static final int INFO_PROTOCOL = 0;
    public static final int INFO_CIPHER = 1;
    public static final int INFO_ALPN = 2;
    public static final int INFO_ERROR = 3;

    /** A session handle, or 0 when TLS is unavailable. alpnCsv lists offered protocols. */
    public static native long create(int fd, String hostname, String alpnCsv, int minVersion);
    public static native int handshake(long handle, int timeoutMs);
    /** Bytes read, 0 at end of stream, or a negative code (-110 on timeout). */
    public static native int read(long handle, byte[] buf, int off, int len, int timeoutMs);
    /** Writes everything: len on success. */
    public static native int write(long handle, byte[] buf, int off, int len, int timeoutMs);
    public static native int available(long handle);
    public static native void shutdown(long handle);
    public static native void destroy(long handle);
    /**
     * The server's chain as {4-byte big-endian length, DER} records, leaf first. Returns the
     * packed size; the bytes are written only when out is at least that long.
     */
    public static native int peerChain(long handle, byte[] out);
    /** Text for one of the INFO_ values; returns its length, 0 when unknown or too long. */
    public static native int info(long handle, int which, byte[] out);
    /**
     * Asks the platform whether a packed chain leads to a trusted root. anchors, when not
     * null, replaces the platform roots. Returns 0 when trusted, else 1 plus the length of the
     * reason written to errOut.
     */
    public static native int evaluate(byte[] chain, int chainLen, byte[] anchors, int anchorsLen,
            byte[] errOut);
}
