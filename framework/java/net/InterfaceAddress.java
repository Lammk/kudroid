package java.net;

public class InterfaceAddress {
    private final InetAddress address;
    private final Inet4Address broadcast;
    private final short prefixLength;

    InterfaceAddress(InetAddress address, Inet4Address broadcast, short prefixLength) {
        this.address = address;
        this.broadcast = broadcast;
        this.prefixLength = prefixLength;
    }

    public InetAddress getAddress() { return address; }
    public InetAddress getBroadcast() { return broadcast; }
    public short getNetworkPrefixLength() { return prefixLength; }
    public boolean equals(Object other) {
        if (!(other instanceof InterfaceAddress)) return false;
        InterfaceAddress value = (InterfaceAddress) other;
        return address.equals(value.address) && prefixLength == value.prefixLength
                && (broadcast == null ? value.broadcast == null : broadcast.equals(value.broadcast));
    }
    public int hashCode() {
        return address.hashCode() + (broadcast == null ? 0 : broadcast.hashCode()) + prefixLength;
    }
    public String toString() { return address + "/" + prefixLength + " [" + broadcast + "]"; }
}
