package java.net;

import java.io.Closeable;
import java.io.IOException;
import libcore.io.KuNet;

/** A TCP listening socket backed by a host socket. */
public class ServerSocket implements Closeable {
    // accept() waits in slices so close() from another thread is noticed promptly.
    private static final int ACCEPT_SLICE_MS = 250;

    private int fd = -1;
    private boolean bound;
    private boolean closed;
    private int soTimeout;
    private boolean reuse = true;
    private final Object lock = new Object();

    public ServerSocket() throws IOException {}

    public ServerSocket(int port) throws IOException {
        this(port, 50, null);
    }

    public ServerSocket(int port, int backlog) throws IOException {
        this(port, backlog, null);
    }

    public ServerSocket(int port, int backlog, InetAddress bindAddr) throws IOException {
        if (port < 0 || port > 65535) throw new IllegalArgumentException("Port out of range: " + port);
        bind(new InetSocketAddress(bindAddr, port), backlog);
    }

    public void bind(SocketAddress endpoint) throws IOException {
        bind(endpoint, 50);
    }

    public void bind(SocketAddress endpoint, int backlog) throws IOException {
        if (closed) throw new SocketException("Socket is closed");
        if (bound) throw new SocketException("Already bound");
        InetSocketAddress isa;
        if (endpoint == null) {
            isa = new InetSocketAddress(0);
        } else if (endpoint instanceof InetSocketAddress) {
            isa = (InetSocketAddress) endpoint;
            if (isa.isUnresolved()) throw new SocketException("Unresolved address");
        } else {
            throw new IllegalArgumentException("Unsupported address type");
        }
        InetAddress a = isa.getAddress();
        byte[] raw = a.isAnyLocalAddress() ? null : a.getAddress();
        int rc = -KuNet.EAFNOSUPPORT;
        if (raw == null) {
            // The wildcard listens on both families through one IPv6 socket.
            rc = open(KuNet.AF_INET6, null, isa.getPort(), true);
        }
        if (rc < 0) {
            int family = (raw != null && raw.length == 16) ? KuNet.AF_INET6 : KuNet.AF_INET;
            rc = open(family, raw, isa.getPort(), false);
        }
        if (rc < 0) {
            throw new BindException(NetUtil.strerror(-rc));
        }
        int listenRc = KuNet.listen(fd, backlog <= 0 ? 50 : backlog);
        if (listenRc < 0) {
            discard();
            throw new SocketException(NetUtil.strerror(-listenRc));
        }
        bound = true;
    }

    private int open(int family, byte[] addr, int port, boolean dual) {
        int r = KuNet.socket(family, KuNet.SOCK_STREAM);
        if (r < 0) return r;
        synchronized (lock) {
            fd = r;
        }
        if (reuse) KuNet.setOption(fd, KuNet.SOL_SOCKET, KuNet.SO_REUSEADDR, 1);
        if (dual) KuNet.setOption(fd, KuNet.IPPROTO_IPV6, KuNet.IPV6_V6ONLY, 0);
        int rc = KuNet.bind(fd, addr, port);
        if (rc < 0) {
            discard();
            return rc;
        }
        return 0;
    }

    private void discard() {
        synchronized (lock) {
            if (fd >= 0) KuNet.close(fd);
            fd = -1;
        }
    }

    private byte[] localName() {
        if (fd < 0 || !bound) return null;
        byte[] peer = new byte[KuNet.PEER_BYTES];
        return KuNet.sockname(fd, peer, false) == 0 ? peer : null;
    }

    public InetAddress getInetAddress() {
        byte[] peer = localName();
        if (peer == null) return null;
        byte[] a = new byte[peer[0] & 0xff];
        System.arraycopy(peer, 1, a, 0, a.length);
        try {
            InetAddress ia = InetAddress.getByAddress(a);
            return ia.isAnyLocalAddress() ? InetAddress.anyLocalAddress() : ia;
        } catch (UnknownHostException e) {
            return null;
        }
    }

    public int getLocalPort() {
        byte[] peer = localName();
        return peer == null ? -1 : ((peer[17] & 0xff) << 8) | (peer[18] & 0xff);
    }

    public SocketAddress getLocalSocketAddress() {
        InetAddress a = getInetAddress();
        return a == null ? null : new InetSocketAddress(a, getLocalPort());
    }

    public Socket accept() throws IOException {
        if (closed) throw new SocketException("Socket is closed");
        if (!bound) throw new SocketException("Socket is not bound yet");
        long deadline = soTimeout > 0 ? System.currentTimeMillis() + soTimeout : 0;
        byte[] peer = new byte[KuNet.PEER_BYTES];
        for (;;) {
            int handle = fd;
            if (closed || handle < 0) throw new SocketException("Socket closed");
            int slice = ACCEPT_SLICE_MS;
            if (deadline != 0) {
                long left = deadline - System.currentTimeMillis();
                if (left <= 0) throw new SocketTimeoutException("Accept timed out");
                if (left < slice) slice = (int) left;
            }
            int r = KuNet.accept(handle, slice, peer);
            if (r >= 0) return new Socket(r, peer);
            if (r != -KuNet.ETIMEDOUT) {
                if (closed) throw new SocketException("Socket closed");
                throw NetUtil.socketError(-r);
            }
        }
    }

    public void close() throws IOException {
        int handle;
        synchronized (lock) {
            if (closed) return;
            closed = true;
            handle = fd;
            fd = -1;
        }
        if (handle >= 0) KuNet.close(handle);
    }

    public boolean isBound() { return bound; }
    public boolean isClosed() { return closed; }

    public synchronized void setSoTimeout(int timeout) throws SocketException {
        if (closed) throw new SocketException("Socket is closed");
        if (timeout < 0) throw new IllegalArgumentException("timeout < 0");
        soTimeout = timeout;
    }

    public synchronized int getSoTimeout() throws IOException {
        if (closed) throw new SocketException("Socket is closed");
        return soTimeout;
    }

    public void setReuseAddress(boolean on) throws SocketException {
        if (closed) throw new SocketException("Socket is closed");
        reuse = on;
        if (fd >= 0) KuNet.setOption(fd, KuNet.SOL_SOCKET, KuNet.SO_REUSEADDR, on ? 1 : 0);
    }

    public boolean getReuseAddress() throws SocketException {
        if (closed) throw new SocketException("Socket is closed");
        return reuse;
    }

    public void setReceiveBufferSize(int size) throws SocketException {
        if (closed) throw new SocketException("Socket is closed");
        if (size <= 0) throw new IllegalArgumentException("size <= 0");
        if (fd >= 0) KuNet.setOption(fd, KuNet.SOL_SOCKET, KuNet.SO_RCVBUF, size);
    }

    public int getReceiveBufferSize() throws SocketException {
        if (closed) throw new SocketException("Socket is closed");
        int v = fd >= 0 ? KuNet.getOption(fd, KuNet.SOL_SOCKET, KuNet.SO_RCVBUF) : 0;
        return v > 0 ? v : 65536;
    }

    @Override
    public String toString() {
        if (!bound) return "ServerSocket[unbound]";
        return "ServerSocket[addr=" + getInetAddress() + ",localport=" + getLocalPort() + "]";
    }
}
