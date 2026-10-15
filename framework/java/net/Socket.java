package java.net;

import java.io.Closeable;
import java.io.FileDescriptor;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import libcore.io.KuNet;

/** A TCP socket backed by a host socket. */
public class Socket implements Closeable {
    private int fd = -1;
    private boolean connected;
    private boolean bound;
    private boolean closed;
    private boolean inputShutdown;
    private boolean outputShutdown;
    private InetAddress remoteAddr;
    private int remotePort;
    private int soTimeout;

    // Options set before the host socket exists are applied when it is created.
    private int optNoDelay = -1;
    private int optKeepAlive = -1;
    private int optReuse = -1;
    private int optOobInline = -1;
    private int optSndBuf = -1;
    private int optRcvBuf = -1;
    private int optLinger = -2;
    private int optTos = -1;

    private InputStream in;
    private OutputStream out;
    private final Object lock = new Object();

    public Socket() {}

    public Socket(Proxy proxy) {
        if (proxy == null) throw new IllegalArgumentException("proxy == null");
    }

    public Socket(String host, int port) throws UnknownHostException, IOException {
        this(host, port, null, 0);
    }

    public Socket(String host, int port, InetAddress localAddr, int localPort) throws IOException {
        InetAddress[] all = host != null ? InetAddress.getAllByName(host)
                : new InetAddress[] {InetAddress.getLoopbackAddress()};
        startup(all, port, localAddr, localPort);
    }

    public Socket(InetAddress address, int port) throws IOException {
        this(address, port, null, 0);
    }

    public Socket(InetAddress address, int port, InetAddress localAddr, int localPort)
            throws IOException {
        if (address == null) throw new NullPointerException("address == null");
        startup(new InetAddress[] {address}, port, localAddr, localPort);
    }

    /** A connected socket handed out by ServerSocket.accept. */
    Socket(int fd, byte[] peer) {
        this.fd = fd;
        this.connected = true;
        this.bound = true;
        try {
            this.remoteAddr = InetAddress.getByAddress(addressOf(peer));
        } catch (UnknownHostException e) {
            this.remoteAddr = null;
        }
        this.remotePort = portOf(peer);
    }

    private void startup(InetAddress[] dests, int port, InetAddress localAddr, int localPort)
            throws IOException {
        if (port < 0 || port > 65535) throw new IllegalArgumentException("port out of range:" + port);
        IOException last = null;
        for (int i = 0; i < dests.length; i++) {
            try {
                if (localAddr != null || localPort > 0) {
                    bind(new InetSocketAddress(localAddr, localPort));
                }
                connect(new InetSocketAddress(dests[i], port), 0);
                return;
            } catch (IOException e) {
                last = e;
                // Try the next address on a fresh host socket.
                if (!closed) discard();
            }
        }
        close();
        if (last != null) throw last;
    }

    private static byte[] addressOf(byte[] peer) {
        int len = peer[0] & 0xff;
        byte[] a = new byte[len];
        System.arraycopy(peer, 1, a, 0, len);
        return a;
    }

    private static int portOf(byte[] peer) {
        return ((peer[17] & 0xff) << 8) | (peer[18] & 0xff);
    }

    // ── host socket ──────────────────────────────────────────────────────────

    private void ensureCreated(int family) throws SocketException {
        synchronized (lock) {
            if (closed) throw new SocketException("Socket is closed");
            if (fd >= 0) return;
            int r = KuNet.socket(family, KuNet.SOCK_STREAM);
            if (r < 0) throw NetUtil.socketError(-r);
            fd = r;
            applyOptions();
        }
    }

    private void discard() {
        synchronized (lock) {
            if (fd >= 0) KuNet.close(fd);
            fd = -1;
            bound = false;
            connected = false;
        }
    }

    private void applyOptions() {
        if (optNoDelay >= 0) KuNet.setOption(fd, KuNet.IPPROTO_TCP, KuNet.TCP_NODELAY, optNoDelay);
        if (optKeepAlive >= 0) KuNet.setOption(fd, KuNet.SOL_SOCKET, KuNet.SO_KEEPALIVE, optKeepAlive);
        if (optReuse >= 0) KuNet.setOption(fd, KuNet.SOL_SOCKET, KuNet.SO_REUSEADDR, optReuse);
        if (optOobInline >= 0) KuNet.setOption(fd, KuNet.SOL_SOCKET, KuNet.SO_OOBINLINE, optOobInline);
        if (optSndBuf > 0) KuNet.setOption(fd, KuNet.SOL_SOCKET, KuNet.SO_SNDBUF, optSndBuf);
        if (optRcvBuf > 0) KuNet.setOption(fd, KuNet.SOL_SOCKET, KuNet.SO_RCVBUF, optRcvBuf);
        if (optLinger != -2) KuNet.setOption(fd, KuNet.SOL_SOCKET, KuNet.SO_LINGER, optLinger);
        if (optTos >= 0) KuNet.setOption(fd, KuNet.IPPROTO_IP, KuNet.IP_TOS, optTos);
    }

    // ── connecting ───────────────────────────────────────────────────────────

    public void bind(SocketAddress bindpoint) throws IOException {
        if (closed) throw new SocketException("Socket is closed");
        if (bound) throw new SocketException("Already bound");
        InetSocketAddress isa;
        if (bindpoint == null) {
            isa = new InetSocketAddress(0);
        } else if (bindpoint instanceof InetSocketAddress) {
            isa = (InetSocketAddress) bindpoint;
            if (isa.isUnresolved()) throw new SocketException("Unresolved address");
        } else {
            throw new IllegalArgumentException("Unsupported address type");
        }
        InetAddress a = isa.getAddress();
        ensureCreated(a.getAddress().length == 16 ? KuNet.AF_INET6 : KuNet.AF_INET);
        int rc = KuNet.bind(fd, a.isAnyLocalAddress() ? null : a.getAddress(), isa.getPort());
        if (rc < 0) {
            throw new BindException(NetUtil.strerror(-rc));
        }
        bound = true;
    }

    public void connect(SocketAddress endpoint) throws IOException {
        connect(endpoint, 0);
    }

    public void connect(SocketAddress endpoint, int timeout) throws IOException {
        if (endpoint == null) throw new IllegalArgumentException("remoteAddr == null");
        if (timeout < 0) throw new IllegalArgumentException("timeout < 0");
        if (closed) throw new SocketException("Socket is closed");
        if (connected) throw new SocketException("Already connected");
        if (!(endpoint instanceof InetSocketAddress)) {
            throw new IllegalArgumentException("Unsupported address type");
        }
        InetSocketAddress isa = (InetSocketAddress) endpoint;
        if (isa.isUnresolved()) throw new UnknownHostException(isa.getHostName());
        InetAddress dest = isa.getAddress();
        byte[] raw = dest.isAnyLocalAddress() ? InetAddress.getLoopbackAddress().getAddress()
                : dest.getAddress();
        ensureCreated(raw.length == 16 ? KuNet.AF_INET6 : KuNet.AF_INET);
        int rc = KuNet.connect(fd, raw, isa.getPort(), timeout);
        if (rc < 0) {
            throw NetUtil.connectError(-rc, dest, isa.getPort(), timeout);
        }
        connected = true;
        bound = true;
        remoteAddr = dest;
        remotePort = isa.getPort();
    }

    // ── identity ─────────────────────────────────────────────────────────────

    public InetAddress getInetAddress() { return connected ? remoteAddr : null; }
    public int getPort() { return connected ? remotePort : 0; }

    private byte[] localName() {
        if (fd < 0 || !bound) return null;
        byte[] peer = new byte[KuNet.PEER_BYTES];
        return KuNet.sockname(fd, peer, false) == 0 ? peer : null;
    }

    public InetAddress getLocalAddress() {
        byte[] peer = localName();
        if (peer == null || peer[0] == 0) return InetAddress.anyLocalAddress();
        try {
            return InetAddress.getByAddress(addressOf(peer));
        } catch (UnknownHostException e) {
            return InetAddress.anyLocalAddress();
        }
    }

    public int getLocalPort() {
        byte[] peer = localName();
        return peer == null ? -1 : portOf(peer);
    }

    public SocketAddress getRemoteSocketAddress() {
        return connected ? new InetSocketAddress(remoteAddr, remotePort) : null;
    }

    public SocketAddress getLocalSocketAddress() {
        return bound ? new InetSocketAddress(getLocalAddress(), getLocalPort()) : null;
    }


    /** The host descriptor, under the name the platform's own TLS stack looks up. */
    public FileDescriptor getFileDescriptor$() {
        FileDescriptor f = new FileDescriptor();
        f.setInt$(fd);
        return f;
    }

    /** The host socket number, or -1 before it exists. */
    public int kuFd() { return fd; }

    /**
     * True when a connection that should be idle has become readable: the peer closed it or
     * sent something unexpected, so it must not carry another request.
     */
    public boolean kuPeerClosed() {
        int handle = fd;
        if (closed || handle < 0) return true;
        return KuNet.pollRead(handle, 0) != 0;
    }

    // ── streams ──────────────────────────────────────────────────────────────

    public InputStream getInputStream() throws IOException {
        if (closed) throw new SocketException("Socket is closed");
        if (!connected) throw new SocketException("Socket is not connected");
        if (inputShutdown) throw new SocketException("Socket input is shutdown");
        synchronized (lock) {
            if (in == null) in = new SocketInputStream();
            return in;
        }
    }

    public OutputStream getOutputStream() throws IOException {
        if (closed) throw new SocketException("Socket is closed");
        if (!connected) throw new SocketException("Socket is not connected");
        if (outputShutdown) throw new SocketException("Socket output is shutdown");
        synchronized (lock) {
            if (out == null) out = new SocketOutputStream();
            return out;
        }
    }

    private final class SocketInputStream extends InputStream {
        private final byte[] one = new byte[1];

        @Override
        public int read() throws IOException {
            int n = read(one, 0, 1);
            return n < 0 ? -1 : (one[0] & 0xff);
        }

        @Override
        public int read(byte[] b) throws IOException {
            return read(b, 0, b.length);
        }

        @Override
        public int read(byte[] b, int off, int len) throws IOException {
            if (b == null) throw new NullPointerException("buffer == null");
            if (off < 0 || len < 0 || off > b.length - len) throw new IndexOutOfBoundsException();
            if (len == 0) return 0;
            int handle = fd;
            if (closed || handle < 0) throw new SocketException("Socket closed");
            if (inputShutdown) return -1;
            int n = KuNet.read(handle, b, off, len, soTimeout);
            if (n > 0) return n;
            if (n == 0) return -1;
            throw NetUtil.ioError(-n, closed);
        }

        @Override
        public int available() throws IOException {
            if (closed || fd < 0) throw new SocketException("Socket closed");
            return inputShutdown ? 0 : KuNet.available(fd);
        }

        @Override
        public void close() throws IOException {
            Socket.this.close();
        }
    }

    private final class SocketOutputStream extends OutputStream {
        private final byte[] one = new byte[1];

        @Override
        public void write(int b) throws IOException {
            one[0] = (byte) b;
            write(one, 0, 1);
        }

        @Override
        public void write(byte[] b) throws IOException {
            write(b, 0, b.length);
        }

        @Override
        public void write(byte[] b, int off, int len) throws IOException {
            if (b == null) throw new NullPointerException("buffer == null");
            if (off < 0 || len < 0 || off > b.length - len) throw new IndexOutOfBoundsException();
            if (len == 0) return;
            int handle = fd;
            if (closed || handle < 0) throw new SocketException("Socket closed");
            if (outputShutdown) throw new SocketException("Socket output is shutdown");
            int n = KuNet.write(handle, b, off, len);
            if (n < 0) throw NetUtil.ioError(-n, closed);
        }

        @Override
        public void flush() {}

        @Override
        public void close() throws IOException {
            Socket.this.close();
        }
    }

    // ── options ──────────────────────────────────────────────────────────────

    private void checkOpen() throws SocketException {
        if (closed) throw new SocketException("Socket is closed");
    }

    public void setTcpNoDelay(boolean on) throws SocketException {
        checkOpen();
        optNoDelay = on ? 1 : 0;
        if (fd >= 0) KuNet.setOption(fd, KuNet.IPPROTO_TCP, KuNet.TCP_NODELAY, optNoDelay);
    }

    public boolean getTcpNoDelay() throws SocketException {
        checkOpen();
        if (fd >= 0) {
            int v = KuNet.getOption(fd, KuNet.IPPROTO_TCP, KuNet.TCP_NODELAY);
            if (v != Integer.MIN_VALUE) return v != 0;
        }
        return optNoDelay > 0;
    }

    public void setSoLinger(boolean on, int linger) throws SocketException {
        checkOpen();
        if (on && linger < 0) throw new IllegalArgumentException("linger < 0");
        optLinger = on ? Math.min(linger, 65535) : -1;
        if (fd >= 0) KuNet.setOption(fd, KuNet.SOL_SOCKET, KuNet.SO_LINGER, optLinger);
    }

    public int getSoLinger() throws SocketException {
        checkOpen();
        if (fd >= 0) {
            int v = KuNet.getOption(fd, KuNet.SOL_SOCKET, KuNet.SO_LINGER);
            if (v != Integer.MIN_VALUE) return v;
        }
        return optLinger == -2 ? -1 : optLinger;
    }

    public void sendUrgentData(int data) throws IOException {
        throw new SocketException("Urgent data not supported");
    }

    public void setOOBInline(boolean on) throws SocketException {
        checkOpen();
        optOobInline = on ? 1 : 0;
        if (fd >= 0) KuNet.setOption(fd, KuNet.SOL_SOCKET, KuNet.SO_OOBINLINE, optOobInline);
    }

    public boolean getOOBInline() throws SocketException {
        checkOpen();
        return optOobInline > 0;
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

    public synchronized void setSendBufferSize(int size) throws SocketException {
        checkOpen();
        if (size <= 0) throw new IllegalArgumentException("size <= 0");
        optSndBuf = size;
        if (fd >= 0) KuNet.setOption(fd, KuNet.SOL_SOCKET, KuNet.SO_SNDBUF, size);
    }

    public synchronized int getSendBufferSize() throws SocketException {
        checkOpen();
        if (fd >= 0) {
            int v = KuNet.getOption(fd, KuNet.SOL_SOCKET, KuNet.SO_SNDBUF);
            if (v > 0) return v;
        }
        return optSndBuf > 0 ? optSndBuf : 65536;
    }

    public synchronized void setReceiveBufferSize(int size) throws SocketException {
        checkOpen();
        if (size <= 0) throw new IllegalArgumentException("size <= 0");
        optRcvBuf = size;
        if (fd >= 0) KuNet.setOption(fd, KuNet.SOL_SOCKET, KuNet.SO_RCVBUF, size);
    }

    public synchronized int getReceiveBufferSize() throws SocketException {
        checkOpen();
        if (fd >= 0) {
            int v = KuNet.getOption(fd, KuNet.SOL_SOCKET, KuNet.SO_RCVBUF);
            if (v > 0) return v;
        }
        return optRcvBuf > 0 ? optRcvBuf : 65536;
    }

    public void setKeepAlive(boolean on) throws SocketException {
        checkOpen();
        optKeepAlive = on ? 1 : 0;
        if (fd >= 0) KuNet.setOption(fd, KuNet.SOL_SOCKET, KuNet.SO_KEEPALIVE, optKeepAlive);
    }

    public boolean getKeepAlive() throws SocketException {
        checkOpen();
        if (fd >= 0) {
            int v = KuNet.getOption(fd, KuNet.SOL_SOCKET, KuNet.SO_KEEPALIVE);
            if (v != Integer.MIN_VALUE) return v != 0;
        }
        return optKeepAlive > 0;
    }

    public void setTrafficClass(int tc) throws SocketException {
        checkOpen();
        if (tc < 0 || tc > 255) throw new IllegalArgumentException("tc is not in the range 0 - 255");
        optTos = tc;
        if (fd >= 0) KuNet.setOption(fd, KuNet.IPPROTO_IP, KuNet.IP_TOS, tc);
    }

    public int getTrafficClass() throws SocketException {
        checkOpen();
        return optTos < 0 ? 0 : optTos;
    }

    public void setReuseAddress(boolean on) throws SocketException {
        checkOpen();
        optReuse = on ? 1 : 0;
        if (fd >= 0) KuNet.setOption(fd, KuNet.SOL_SOCKET, KuNet.SO_REUSEADDR, optReuse);
    }

    public boolean getReuseAddress() throws SocketException {
        checkOpen();
        if (fd >= 0) {
            int v = KuNet.getOption(fd, KuNet.SOL_SOCKET, KuNet.SO_REUSEADDR);
            if (v != Integer.MIN_VALUE) return v != 0;
        }
        return optReuse > 0;
    }

    public void setPerformancePreferences(int connectionTime, int latency, int bandwidth) {}

    // ── lifecycle ────────────────────────────────────────────────────────────

    public void shutdownInput() throws IOException {
        checkOpen();
        if (!connected) throw new SocketException("Socket is not connected");
        if (inputShutdown) throw new SocketException("Socket input is already shutdown");
        KuNet.shutdown(fd, KuNet.SHUT_RD);
        inputShutdown = true;
    }

    public void shutdownOutput() throws IOException {
        checkOpen();
        if (!connected) throw new SocketException("Socket is not connected");
        if (outputShutdown) throw new SocketException("Socket output is already shutdown");
        KuNet.shutdown(fd, KuNet.SHUT_WR);
        outputShutdown = true;
    }

    public synchronized void close() throws IOException {
        int handle;
        synchronized (lock) {
            if (closed) return;
            closed = true;
            handle = fd;
            fd = -1;
        }
        if (handle >= 0) KuNet.close(handle);
    }

    public boolean isConnected() { return connected; }
    public boolean isBound() { return bound; }
    public boolean isClosed() { return closed; }
    public boolean isInputShutdown() { return inputShutdown; }
    public boolean isOutputShutdown() { return outputShutdown; }

    @Override
    public String toString() {
        if (connected) {
            return "Socket[address=" + remoteAddr + ",port=" + remotePort + ",localPort="
                    + getLocalPort() + "]";
        }
        return "Socket[unconnected]";
    }
}
