package java.net;

import java.util.Enumeration;
import java.util.Collections;
import java.util.List;
import java.util.ArrayList;

public final class NetworkInterface {
    private final String name;
    private final String displayName;

    NetworkInterface(String name, String displayName) {
        this.name = name;
        this.displayName = displayName;
    }
    public String getName() { return name; }
    public String getDisplayName() { return displayName; }
    public static NetworkInterface getByName(String name) throws SocketException {
        return new NetworkInterface(name, name);
    }
    public static NetworkInterface getByInetAddress(InetAddress addr) throws SocketException {
        return new NetworkInterface("en0", "en0");
    }
    public static Enumeration<NetworkInterface> getNetworkInterfaces() throws SocketException {
        List<NetworkInterface> list = new ArrayList<NetworkInterface>();
        list.add(new NetworkInterface("en0", "en0"));
        list.add(new NetworkInterface("lo0", "lo0"));
        return Collections.enumeration(list);
    }
    public Enumeration<InetAddress> getInetAddresses() {
        return Collections.<InetAddress>emptyEnumeration();
    }
    public boolean isUp() throws SocketException { return true; }
    public boolean isLoopback() throws SocketException { return false; }
    public byte[] getHardwareAddress() throws SocketException { return new byte[]{0x02, 0x00, 0x00, 0x00, 0x00, 0x01}; }
    public int getMTU() throws SocketException { return 1500; }
}
