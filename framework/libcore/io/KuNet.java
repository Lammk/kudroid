package libcore.io;

/**
 * Sockets and DNS behind java.net, implemented by the runtime.
 *
 * Arguments use Linux numbers (AF_INET6 is 10, SOL_SOCKET is 1) and every result is either
 * non-negative or the negated Linux errno. Addresses are byte arrays of 4 or 16 bytes; a
 * peer address comes back as {length, 16 address bytes, port high, port low}.
 */
public final class KuNet {
    private KuNet() {}

    public static final int AF_INET = 2;
    public static final int AF_INET6 = 10;
    public static final int SOCK_STREAM = 1;
    public static final int SOCK_DGRAM = 2;

    public static final int IPPROTO_IP = 0;
    public static final int IPPROTO_TCP = 6;
    public static final int IPPROTO_IPV6 = 41;
    public static final int SOL_SOCKET = 1;

    public static final int SO_REUSEADDR = 2;
    public static final int SO_BROADCAST = 6;
    public static final int SO_SNDBUF = 7;
    public static final int SO_RCVBUF = 8;
    public static final int SO_KEEPALIVE = 9;
    public static final int SO_OOBINLINE = 10;
    public static final int SO_LINGER = 13;
    public static final int TCP_NODELAY = 1;
    public static final int IP_TOS = 1;
    public static final int IP_MULTICAST_TTL = 33;
    public static final int IPV6_V6ONLY = 26;

    public static final int SHUT_RD = 0;
    public static final int SHUT_WR = 1;
    public static final int SHUT_RDWR = 2;

    public static final int EPERM = 1;
    public static final int EBADF = 9;
    public static final int EACCES = 13;
    public static final int EINVAL = 22;
    public static final int EPIPE = 32;
    public static final int EAFNOSUPPORT = 97;
    public static final int EADDRINUSE = 98;
    public static final int EADDRNOTAVAIL = 99;
    public static final int ENETDOWN = 100;
    public static final int ENETUNREACH = 101;
    public static final int ECONNABORTED = 103;
    public static final int ECONNRESET = 104;
    public static final int ENOTCONN = 107;
    public static final int ETIMEDOUT = 110;
    public static final int ECONNREFUSED = 111;
    public static final int EHOSTUNREACH = 113;

    /** Length of the peer array filled by accept, sockname and recvFrom. */
    public static final int PEER_BYTES = 19;

    public static native int socket(int domain, int type);
    public static native int connect(int fd, byte[] addr, int port, int timeoutMs);
    public static native int bind(int fd, byte[] addr, int port);
    public static native int listen(int fd, int backlog);
    public static native int accept(int fd, int timeoutMs, byte[] peer);
    /** Bytes read, 0 at end of stream, or -errno (-ETIMEDOUT when timeoutMs elapses). */
    public static native int read(int fd, byte[] buf, int off, int len, int timeoutMs);
    /** Writes everything or fails: len on success, -errno otherwise. */
    public static native int write(int fd, byte[] buf, int off, int len);
    public static native int close(int fd);
    public static native int shutdown(int fd, int how);
    public static native int sockname(int fd, byte[] out, boolean peer);
    public static native int setOption(int fd, int level, int opt, int value);
    /** The option value, or Integer.MIN_VALUE on failure. */
    public static native int getOption(int fd, int level, int opt);
    public static native int available(int fd);
    /** 1 when readable (data, end of stream or error), 0 on timeout, or -errno. */
    public static native int pollRead(int fd, int timeoutMs);
    public static native int sendTo(int fd, byte[] buf, int off, int len, byte[] addr, int port);
    public static native int recvFrom(int fd, byte[] buf, int off, int len, int timeoutMs, byte[] peer);
    /** Fills out with 17-byte entries {length, 16 address bytes}; returns the count or -1. */
    public static native int resolve(String host, byte[] out);
    /** Writes the host name for addr into out; returns its length, 0 when there is none. */
    public static native int reverse(byte[] addr, byte[] out);
    public static native int hostName(byte[] out);
    public static native int joinGroup(int fd, byte[] group, boolean join);
}
