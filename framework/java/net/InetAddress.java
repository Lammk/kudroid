package java.net;

import java.io.Serializable;

public class InetAddress implements Serializable {
    private static final long serialVersionUID = 3286316764910316507L;
    String hostName;
    int address;

    InetAddress() {}
    InetAddress(String hostName, int address) {
        this.hostName = hostName;
        this.address = address;
    }

    public String getHostName() { return hostName != null ? hostName : "localhost"; }
    public String getHostAddress() { return "127.0.0.1"; }
    public byte[] getAddress() { return new byte[]{127, 0, 0, 1}; }
    public static InetAddress getByName(String host) throws UnknownHostException {
        if (host == null || host.isEmpty() || host.equals("localhost") || host.equals("127.0.0.1")) {
            return new InetAddress(host, 0x7f000001);
        }
        return new InetAddress(host, 0);
    }
    public static InetAddress[] getAllByName(String host) throws UnknownHostException {
        return new InetAddress[]{ getByName(host) };
    }
    public static InetAddress getLocalHost() throws UnknownHostException {
        return new InetAddress("localhost", 0x7f000001);
    }
    public static InetAddress getByAddress(byte[] addr) throws UnknownHostException {
        return new InetAddress("localhost", 0x7f000001);
    }
    public static InetAddress getByAddress(String host, byte[] addr) throws UnknownHostException {
        return new InetAddress(host, 0x7f000001);
    }
    public boolean isLoopbackAddress() { return true; }
    public String toString() { return getHostName() + "/" + getHostAddress(); }
    public int hashCode() { return address; }
    public boolean equals(Object obj) {
        return (obj instanceof InetAddress) && ((InetAddress) obj).address == address;
    }
}
