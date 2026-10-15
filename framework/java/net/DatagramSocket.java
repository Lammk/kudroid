package java.net;

import java.io.Closeable;
import java.io.IOException;
import libcore.io.KuNet;

/** A UDP socket backed by a host socket (IPv4 unless bound to an IPv6 address). */
public class DatagramSocket implements Closeable {
    int fd = -1;
    private boolean bound;
    private boolean closed;
    private boolean connected;
    private InetAddress connectedAddr;
    private int connectedPort = -1;
    private int soTimeout;
    private final Object lock = new Object();

    public DatagramSocket() throws SocketException {
        this((SocketAddress) new InetSocketAddress(0));
    }

    public DatagramSocket(SocketAddress bindaddr) throws SocketException {
        if (bindaddr != null) bind(bindaddr);
    }

    public DatagramSocket(int port) throws SocketException {
        this(port, null);
    }

    public DatagramSocket(int port, InetAddress laddr) throws SocketException {
        if (port < 0 || port > 65535) throw new IllegalArgumentException("Port out of range:" + port);
        bind(new InetSocketAddress(laddr, port));
    }

    public synchronized void bind(SocketAddress addr) throws SocketException {
        if (closed) throw new SocketException("Socket is closed");
        if (bound) throw new SocketException("already bound");
        InetSocketAddress isa;
        if (addr == null) {
            isa = new InetSocketAddress(0);
        } else if (addr instanceof InetSocketAddress) {
            isa = (InetSocketAddress) addr;
            if (isa.isUnresolved()) throw new SocketException("Unresolved address");
        } else {
            throw new IllegalArgumentException("Unsupported address type!");
        }
        InetAddress a = isa.getAddress();
        byte[] raw = a.isAnyLocalAddress() ? null : a.getAddress();
        int family = (raw != null && raw.length == 16) ? KuNet.AF_INET6 : KuNet.AF_INET;
        int r = KuNet.socket(family, KuNet.SOCK_DGRAM);
        if (r < 0) throw NetUtil.socketError(-r);
        fd = r;
        KuNet.setOption(fd, KuNet.SOL_SOCKET, KuNet.SO_REUSEADDR, 1);
        int rc = KuNet.bind(fd, raw, isa.getPort());
        if (rc < 0) {
            KuNet.close(fd);
            fd = -1;
            throw new BindException(NetUtil.strerror(-rc));
        }
        bound = true;
    }

    public void connect(SocketAddress addr) throws SocketException {
        if (!(addr instanceof InetSocketAddress)) {
            throw new IllegalArgumentException("Unsupported address type");
        }
        InetSocketAddress isa = (InetSocketAddress) addr;
        if (isa.isUnresolved()) throw new SocketException("Unresolved address");
        connect(isa.getAddress(), isa.getPort());
    }

    public void connect(InetAddress address, int port) {
        if (address == null) throw new IllegalArgumentException("Address can't be null");
        if (port < 0 || port > 65535) throw new IllegalArgumentException("Port out of range:" + port);
        connectedAddr = address;
        connectedPort = port;
        connected = true;
    }

    public void disconnect() {
        connected = false;
        connectedAddr = null;
        connectedPort = -1;
    }

    public boolean isConnected() { return connected; }
    public InetAddress getInetAddress() { return connectedAddr; }
    public int getPort() { return connectedPort; }
    public SocketAddress getRemoteSocketAddress() {
        return connected ? new InetSocketAddress(connectedAddr, connectedPort) : null;
    }

    private byte[] localName() {
        if (fd < 0 || !bound) return null;
        byte[] peer = new byte[KuNet.PEER_BYTES];
        return KuNet.sockname(fd, peer, false) == 0 ? peer : null;
    }

    public InetAddress getLocalAddress() {
        byte[] peer = localName();
        if (peer == null || peer[0] == 0) return InetAddress.anyLocalAddress();
        byte[] a = new byte[peer[0] & 0xff];
        System.arraycopy(peer, 1, a, 0, a.length);
        try {
            return InetAddress.getByAddress(a);
        } catch (UnknownHostException e) {
            return InetAddress.anyLocalAddress();
        }
    }

    public int getLocalPort() {
        if (closed) return -1;
        byte[] peer = localName();
        return peer == null ? 0 : ((peer[17] & 0xff) << 8) | (peer[18] & 0xff);
    }

    public SocketAddress getLocalSocketAddress() {
        return bound && !closed ? new InetSocketAddress(getLocalAddress(), getLocalPort()) : null;
    }

    public boolean isBound() { return bound; }
    public boolean isClosed() { return closed; }

    public void send(DatagramPacket p) throws IOException {
        if (closed) throw new SocketException("Socket is closed");
        InetAddress dest = p.getAddress();
        int port = p.getPort();
        if (dest == null) {
            if (!connected) throw new IllegalArgumentException("Address not set");
            dest = connectedAddr;
            port = connectedPort;
        } else if (connected && (!dest.equals(connectedAddr) || port != connectedPort)) {
            throw new IllegalArgumentException("connected address and packet address differ");
        }
        if (port < 0) throw new IllegalArgumentException("Port not set");
        if (!bound) bind(null);
        int n = KuNet.sendTo(fd, p.getData(), p.getOffset(), p.getLength(), dest.getAddress(), port);
        if (n < 0) throw NetUtil.socketError(-n);
    }

    public synchronized void receive(DatagramPacket p) throws IOException {
        if (closed) throw new SocketException("Socket is closed");
        if (!bound) bind(null);
        byte[] peer = new byte[KuNet.PEER_BYTES];
        int handle = fd;
        int n = KuNet.recvFrom(handle, p.getData(), p.getOffset(), p.bufLength(), soTimeout, peer);
        if (n < 0) {
            if (closed) throw new SocketException("Socket closed");
            if (n == -KuNet.ETIMEDOUT) throw new SocketTimeoutException("Receive timed out");
            throw NetUtil.socketError(-n);
        }
        byte[] a = new byte[peer[0] & 0xff];
        System.arraycopy(peer, 1, a, 0, a.length);
        InetAddress from = a.length == 0 ? null : InetAddress.getByAddress(a);
        p.received(n, from, ((peer[17] & 0xff) << 8) | (peer[18] & 0xff));
    }

    public void close() {
        int handle;
        synchronized (lock) {
            if (closed) return;
            closed = true;
            handle = fd;
            fd = -1;
        }
        if (handle >= 0) KuNet.close(handle);
    }

    private void checkOpen() throws SocketException {
        if (closed) throw new SocketException("Socket is closed");
    }

    public synchronized void setSoTimeout(int timeout) throws SocketException {
        checkOpen();
        if (timeout < 0) throw new IllegalArgumentException("timeout < 0");
        soTimeout = timeout;
    }

    public synchronized int getSoTimeout() throws SocketException {
        checkOpen();
        return soTimeout;
    }

    public void setBroadcast(boolean on) throws SocketException {
        checkOpen();
        if (fd >= 0) KuNet.setOption(fd, KuNet.SOL_SOCKET, KuNet.SO_BROADCAST, on ? 1 : 0);
    }

    public boolean getBroadcast() throws SocketException {
        checkOpen();
        return fd >= 0 && KuNet.getOption(fd, KuNet.SOL_SOCKET, KuNet.SO_BROADCAST) > 0;
    }

    public void setReuseAddress(boolean on) throws SocketException {
        checkOpen();
        if (fd >= 0) KuNet.setOption(fd, KuNet.SOL_SOCKET, KuNet.SO_REUSEADDR, on ? 1 : 0);
    }

    public boolean getReuseAddress() throws SocketException {
        checkOpen();
        return fd >= 0 && KuNet.getOption(fd, KuNet.SOL_SOCKET, KuNet.SO_REUSEADDR) > 0;
    }

    public synchronized void setReceiveBufferSize(int size) throws SocketException {
        checkOpen();
        if (size <= 0) throw new IllegalArgumentException("size <= 0");
        if (fd >= 0) KuNet.setOption(fd, KuNet.SOL_SOCKET, KuNet.SO_RCVBUF, size);
    }

    public synchronized int getReceiveBufferSize() throws SocketException {
        checkOpen();
        int v = fd >= 0 ? KuNet.getOption(fd, KuNet.SOL_SOCKET, KuNet.SO_RCVBUF) : 0;
        return v > 0 ? v : 65536;
    }

    public synchronized void setSendBufferSize(int size) throws SocketException {
        checkOpen();
        if (size <= 0) throw new IllegalArgumentException("size <= 0");
        if (fd >= 0) KuNet.setOption(fd, KuNet.SOL_SOCKET, KuNet.SO_SNDBUF, size);
    }

    public synchronized int getSendBufferSize() throws SocketException {
        checkOpen();
        int v = fd >= 0 ? KuNet.getOption(fd, KuNet.SOL_SOCKET, KuNet.SO_SNDBUF) : 0;
        return v > 0 ? v : 65536;
    }

    public void setTrafficClass(int tc) throws SocketException {
        checkOpen();
        if (fd >= 0) KuNet.setOption(fd, KuNet.IPPROTO_IP, KuNet.IP_TOS, tc);
    }

    public int getTrafficClass() throws SocketException {
        checkOpen();
        return 0;
    }
}
