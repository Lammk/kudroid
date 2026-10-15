package java.net;

import java.io.IOException;
import libcore.io.KuNet;

/** Maps runtime errno values onto the exceptions java.net throws. */
final class NetUtil {
    private NetUtil() {}

    static String strerror(int err) {
        switch (err) {
            case KuNet.EPERM: return "Operation not permitted";
            case KuNet.EBADF: return "Bad file descriptor";
            case KuNet.EACCES: return "Permission denied";
            case KuNet.EINVAL: return "Invalid argument";
            case KuNet.EPIPE: return "Broken pipe";
            case KuNet.EAFNOSUPPORT: return "Address family not supported by protocol";
            case KuNet.EADDRINUSE: return "Address already in use";
            case KuNet.EADDRNOTAVAIL: return "Cannot assign requested address";
            case KuNet.ENETDOWN: return "Network is down";
            case KuNet.ENETUNREACH: return "Network is unreachable";
            case KuNet.ECONNABORTED: return "Software caused connection abort";
            case KuNet.ECONNRESET: return "Connection reset by peer";
            case KuNet.ENOTCONN: return "Transport endpoint is not connected";
            case KuNet.ETIMEDOUT: return "Connection timed out";
            case KuNet.ECONNREFUSED: return "Connection refused";
            case KuNet.EHOSTUNREACH: return "No route to host";
            default: return "errno " + err;
        }
    }

    static SocketException socketError(int err) {
        if (err == KuNet.ECONNRESET) return new SocketException("Connection reset");
        return new SocketException(strerror(err));
    }

    /** The exception for a failed connect to addr:port. */
    static IOException connectError(int err, InetAddress addr, int port, int timeoutMs) {
        String where = "failed to connect to " + addr + " (port " + port + ")";
        switch (err) {
            case KuNet.ETIMEDOUT:
                return new SocketTimeoutException(where + (timeoutMs > 0 ? " after " + timeoutMs + "ms" : ""));
            case KuNet.ENETUNREACH:
            case KuNet.EHOSTUNREACH:
                return new NoRouteToHostException(where + ": " + strerror(err));
            case KuNet.EADDRINUSE:
            case KuNet.EADDRNOTAVAIL:
                return new BindException(where + ": " + strerror(err));
            default:
                return new ConnectException(where + ": " + strerror(err));
        }
    }

    /** The exception for a failed read or write. */
    static IOException ioError(int err, boolean closed) {
        if (closed) return new SocketException("Socket closed");
        if (err == KuNet.ETIMEDOUT) return new SocketTimeoutException("Read timed out");
        return socketError(err);
    }
}
