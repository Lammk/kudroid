package java.net;

public class Proxy {
    public enum Type { DIRECT, HTTP, SOCKS };
    private final Type type;
    private final SocketAddress sa;
    public static final Proxy NO_PROXY = new Proxy(Type.DIRECT, null);

    public Proxy(Type type, SocketAddress sa) {
        this.type = type;
        this.sa = sa;
    }
    public Type type() { return type; }
    public SocketAddress address() { return sa; }
    public String toString() { return type == Type.DIRECT ? "DIRECT" : type + " @ " + sa; }
}
