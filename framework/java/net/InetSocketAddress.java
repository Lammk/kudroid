package java.net;

public class InetSocketAddress extends SocketAddress {
    private static final long serialVersionUID = 5076001401234631231L;
    private final String hostname;
    private final InetAddress addr;
    private final int port;

    public InetSocketAddress(int port) {
        this((InetAddress) null, port);
    }
    public InetSocketAddress(InetAddress addr, int port) {
        this.addr = addr;
        this.hostname = addr != null ? addr.getHostName() : null;
        this.port = port;
    }
    public InetSocketAddress(String hostname, int port) {
        this.hostname = hostname;
        this.addr = null;
        this.port = port;
    }
    public static InetSocketAddress createUnresolved(String host, int port) {
        return new InetSocketAddress(host, port);
    }
    public final int getPort() { return port; }
    public final InetAddress getAddress() { return addr; }
    public final String getHostName() { return hostname != null ? hostname : (addr != null ? addr.getHostName() : null); }
    public final String getHostString() { return getHostName(); }
    public final boolean isUnresolved() { return addr == null; }
    public String toString() { return getHostString() + ":" + port; }
}
