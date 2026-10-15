package java.net;

public interface SocketOptions {
    int TCP_NODELAY = 0x0001;
    int SO_BINDADDR = 0x000F;
    int SO_REUSEADDR = 0x04;
    int SO_BROADCAST = 0x0020;
    int SO_OOBINLINE = 0x1003;
    int IP_MULTICAST_IF = 0x10;
    int IP_MULTICAST_IF2 = 0x1f;
    int IP_MULTICAST_LOOP = 0x12;
    int IP_TOS = 0x3;
    int SO_LINGER = 0x0080;
    int SO_TIMEOUT = 0x1006;
    int SO_SNDBUF = 0x1001;
    int SO_RCVBUF = 0x1002;
    int SO_KEEPALIVE = 0x0008;
    int SO_DONTROUTE = 0x0010;

    void setOption(int optID, Object value) throws SocketException;
    Object getOption(int optID) throws SocketException;
}
