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
        if (name == null) throw new NullPointerException("name == null");
        Enumeration<NetworkInterface> all = getNetworkInterfaces();
        while (all.hasMoreElements()) {
            NetworkInterface next = all.nextElement();
            if (next.name.equals(name)) return next;
        }
        return null;
    }
    public static NetworkInterface getByInetAddress(InetAddress addr) throws SocketException {
        if (addr == null) throw new NullPointerException("addr == null");
        Enumeration<NetworkInterface> all = getNetworkInterfaces();
        while (all.hasMoreElements()) {
            NetworkInterface next = all.nextElement();
            for (InterfaceAddress address : next.getInterfaceAddresses()) {
                if (address.getAddress().equals(addr)) return next;
            }
        }
        return null;
    }
    public static Enumeration<NetworkInterface> getNetworkInterfaces() throws SocketException {
        List<NetworkInterface> list = new ArrayList<NetworkInterface>();
        String[] names = libcore.io.KuNet.interfaceNames();
        if (names == null) throw new SocketException("Unable to enumerate network interfaces");
        for (String name : names) list.add(new NetworkInterface(name, name));
        return Collections.enumeration(list);
    }
    public Enumeration<InetAddress> getInetAddresses() {
        List<InetAddress> list = new ArrayList<InetAddress>();
        for (InterfaceAddress address : getInterfaceAddresses()) list.add(address.getAddress());
        return Collections.enumeration(list);
    }
    public List<InterfaceAddress> getInterfaceAddresses() {
        List<InterfaceAddress> list = new ArrayList<InterfaceAddress>();
        byte[][] rows = libcore.io.KuNet.interfaceAddresses(name);
        if (rows == null) return list;
        for (byte[] row : rows) {
            int length = row[0] & 255;
            byte[] bytes = new byte[length];
            System.arraycopy(row, 7, bytes, 0, length);
            int scope = ((row[3] & 255) << 24) | ((row[4] & 255) << 16)
                    | ((row[5] & 255) << 8) | (row[6] & 255);
            try {
                InetAddress address = length == 16 ? Inet6Address.getByAddress(null, bytes, scope)
                        : InetAddress.getByAddress(bytes);
                Inet4Address broadcast = null;
                if (row[2] != 0) {
                    byte[] destination = new byte[4];
                    System.arraycopy(row, 7 + length, destination, 0, 4);
                    broadcast = (Inet4Address) InetAddress.getByAddress(destination);
                }
                list.add(new InterfaceAddress(address, broadcast, (short) (row[1] & 255)));
            } catch (UnknownHostException e) { throw new IllegalStateException(e); }
        }
        return list;
    }
    public boolean isUp() throws SocketException { return true; }
    public boolean isLoopback() throws SocketException { return false; }
    public byte[] getHardwareAddress() throws SocketException { return new byte[]{0x02, 0x00, 0x00, 0x00, 0x00, 0x01}; }
    public int getMTU() throws SocketException { return 1500; }
}
