package java.net;

public final class Inet6Address extends InetAddress {
    private static final long serialVersionUID = 6880410070516793377L;
    private int scopeId;

    Inet6Address() { super(); }
    Inet6Address(String hostName, byte[] addr) { super(hostName, addr); }

    public static Inet6Address getByAddress(String host, byte[] addr, int scopeId)
            throws UnknownHostException {
        if (addr == null || addr.length != 16) {
            throw new UnknownHostException("addr is of illegal length");
        }
        Inet6Address a = new Inet6Address(host, addr.clone());
        a.scopeId = scopeId < 0 ? 0 : scopeId;
        return a;
    }

    public int getScopeId() { return scopeId; }
    public NetworkInterface getScopedInterface() { return null; }

    public boolean isIPv4CompatibleAddress() {
        for (int i = 0; i < 12; i++) {
            if (addr[i] != 0) return false;
        }
        return true;
    }

    @Override
    public String getHostAddress() {
        String text = super.getHostAddress();
        return scopeId != 0 ? text + "%" + scopeId : text;
    }
}
