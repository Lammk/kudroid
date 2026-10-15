package java.net;

public class InetSocketAddress extends SocketAddress {
    private static final long serialVersionUID = 5076001401234631231L;
    private final String hostname;
    private final InetAddress addr;
    private final int port;

    private static int checkPort(int port) {
        if (port < 0 || port > 0xFFFF) {
            throw new IllegalArgumentException("port out of range:" + port);
        }
        return port;
    }

    public InetSocketAddress(int port) {
        this((InetAddress) null, port);
    }

    public InetSocketAddress(InetAddress addr, int port) {
        this.port = checkPort(port);
        this.addr = addr != null ? addr : InetAddress.anyLocalAddress();
        this.hostname = null;
    }

    public InetSocketAddress(String hostname, int port) {
        if (hostname == null) throw new IllegalArgumentException("hostname can't be null");
        this.port = checkPort(port);
        InetAddress resolved = null;
        try {
            resolved = InetAddress.getByName(hostname);
        } catch (UnknownHostException ignored) {
            // stays unresolved
        }
        this.addr = resolved;
        this.hostname = hostname;
    }

    private InetSocketAddress(String hostname, int port, boolean unresolved) {
        this.hostname = hostname;
        this.addr = null;
        this.port = port;
    }

    public static InetSocketAddress createUnresolved(String host, int port) {
        if (host == null) throw new IllegalArgumentException("hostname can't be null");
        return new InetSocketAddress(host, checkPort(port), true);
    }

    public final int getPort() { return port; }
    public final InetAddress getAddress() { return addr; }

    public final String getHostName() {
        if (hostname != null) return hostname;
        return addr != null ? addr.getHostName() : null;
    }

    public final String getHostString() {
        if (hostname != null) return hostname;
        if (addr == null) return null;
        return addr.hostName != null ? addr.hostName : addr.getHostAddress();
    }

    public final boolean isUnresolved() { return addr == null; }

    @Override
    public String toString() {
        if (addr == null) return hostname + "/<unresolved>:" + port;
        return addr.toString() + ":" + port;
    }

    @Override
    public final boolean equals(Object obj) {
        if (!(obj instanceof InetSocketAddress)) return false;
        InetSocketAddress o = (InetSocketAddress) obj;
        if (port != o.port) return false;
        if (addr != null) return addr.equals(o.addr);
        return o.addr == null && hostname != null && hostname.equalsIgnoreCase(o.hostname);
    }

    @Override
    public final int hashCode() {
        return (addr != null ? addr.hashCode() : (hostname != null ? hostname.toLowerCase().hashCode() : 0))
                + port;
    }
}
