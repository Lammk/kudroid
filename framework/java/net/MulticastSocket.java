package java.net;

import java.io.IOException;
import libcore.io.KuNet;

public class MulticastSocket extends DatagramSocket {
    public MulticastSocket() throws IOException { super(); }
    public MulticastSocket(int port) throws IOException { super(port); }
    public MulticastSocket(SocketAddress bindaddr) throws IOException { super(bindaddr); }

    public void joinGroup(InetAddress mcastaddr) throws IOException {
        membership(mcastaddr, true);
    }

    public void leaveGroup(InetAddress mcastaddr) throws IOException {
        membership(mcastaddr, false);
    }

    private void membership(InetAddress group, boolean join) throws IOException {
        if (isClosed()) throw new SocketException("Socket is closed");
        if (group == null || !group.isMulticastAddress()) {
            throw new SocketException("Not a multicast address");
        }
        byte[] raw = group.getAddress();
        if (raw.length != 4) throw new SocketException("IPv6 multicast is not supported");
        int rc = KuNet.joinGroup(fd, raw, join);
        if (rc < 0) throw NetUtil.socketError(-rc);
    }

    public void setTimeToLive(int ttl) throws IOException {
        if (isClosed()) throw new SocketException("Socket is closed");
        if (ttl < 0 || ttl > 255) throw new IllegalArgumentException("ttl out of range");
        KuNet.setOption(fd, KuNet.IPPROTO_IP, KuNet.IP_MULTICAST_TTL, ttl);
    }

    public int getTimeToLive() throws IOException {
        if (isClosed()) throw new SocketException("Socket is closed");
        int v = KuNet.getOption(fd, KuNet.IPPROTO_IP, KuNet.IP_MULTICAST_TTL);
        return v == Integer.MIN_VALUE ? 1 : v;
    }
}
