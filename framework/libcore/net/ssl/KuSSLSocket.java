package libcore.net.ssl;

import java.io.FileDescriptor;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.net.SocketAddress;
import java.net.SocketException;
import java.net.SocketTimeoutException;
import java.security.cert.CertificateException;
import java.security.cert.X509Certificate;
import java.util.ArrayList;
import java.util.List;
import javax.net.ssl.HandshakeCompletedEvent;
import javax.net.ssl.HandshakeCompletedListener;
import javax.net.ssl.SSLException;
import javax.net.ssl.SSLHandshakeException;
import javax.net.ssl.SSLParameters;
import javax.net.ssl.SSLSession;
import javax.net.ssl.SSLSocket;
import javax.net.ssl.X509TrustManager;
import libcore.io.KuNet;
import libcore.io.KuTls;

/**
 * A TLS client socket layered over a connected socket.
 *
 * The runtime encrypts and decrypts; the server's certificate chain is judged here, by the
 * trust manager the socket factory was built with, exactly where JSSE would. The wrapped
 * socket owns the descriptor and stays the source of addresses and options.
 */
public final class KuSSLSocket extends SSLSocket {
    private static final int DEFAULT_HANDSHAKE_MS = 60000;

    private final Socket under;
    private final boolean autoClose;
    private final X509TrustManager trustManager;
    private String host;
    private int port;

    private final Object lock = new Object();
    private long handle;
    private boolean handshaken;
    private boolean closed;
    private KuSSLSession session;
    private String negotiatedAlpn;
    private String[] protocols = KuSSLSocketFactory.DEFAULT_PROTOCOLS.clone();
    private String[] suites = KuSSLSocketFactory.DEFAULT_SUITES.clone();
    private String[] alpn = new String[0];
    private final ArrayList<HandshakeCompletedListener> listeners = new ArrayList<HandshakeCompletedListener>();
    private InputStream in;
    private OutputStream out;

    public KuSSLSocket(Socket under, String host, int port, boolean autoClose, X509TrustManager tm) {
        this.under = under;
        this.host = host;
        this.port = port;
        this.autoClose = autoClose;
        this.trustManager = tm;
    }

    // ── handshake ────────────────────────────────────────────────────────────

    @Override
    public void startHandshake() throws IOException {
        synchronized (lock) {
            if (handshaken) return;
            if (closed) throw new SocketException("Socket is closed");
            int fd = under.kuFd();
            if (fd < 0 || !under.isConnected()) throw new SocketException("Socket is not connected");

            String sni = serverName();
            handle = KuTls.create(fd, sni == null ? "" : sni, alpnCsv(), minVersion());
            if (handle == 0) throw new SSLException("TLS is not available");
            int timeout = under.getSoTimeout();
            int rc = KuTls.handshake(handle, timeout > 0 ? timeout : DEFAULT_HANDSHAKE_MS);
            if (rc != 0) {
                String why = info(KuTls.INFO_ERROR);
                destroyHandle();
                if (rc == -KuNet.ETIMEDOUT) throw new SocketTimeoutException("Handshake timed out");
                if (rc == KuTls.HANDSHAKE_FAILED || rc == KuTls.PROTOCOL_ERROR) {
                    throw new SSLHandshakeException(why.length() > 0 ? why : "Handshake failed");
                }
                throw new SSLException("TLS handshake failed: " + (why.length() > 0 ? why : "error " + rc));
            }

            X509Certificate[] chain = peerChain();
            String cipher = info(KuTls.INFO_CIPHER);
            if (chain.length == 0) {
                destroyHandle();
                throw new SSLHandshakeException("Server presented no certificate");
            }
            try {
                trustManager.checkServerTrusted(chain, authType(cipher));
            } catch (CertificateException e) {
                destroyHandle();
                SSLHandshakeException failure = new SSLHandshakeException(e.getMessage());
                failure.initCause(e);
                throw failure;
            }
            negotiatedAlpn = info(KuTls.INFO_ALPN);
            session = new KuSSLSession(host != null ? host : under.getInetAddress().getHostAddress(),
                    port >= 0 ? port : under.getPort(), info(KuTls.INFO_PROTOCOL), cipher, chain);
            handshaken = true;
        }
        HandshakeCompletedListener[] copy;
        synchronized (listeners) {
            copy = listeners.toArray(new HandshakeCompletedListener[listeners.size()]);
        }
        for (int i = 0; i < copy.length; i++) {
            copy[i].handshakeCompleted(new HandshakeCompletedEvent(this, session));
        }
    }

    private String serverName() {
        if (host == null) return null;
        // An address literal is not a server name.
        if (host.indexOf(':') >= 0) return null;
        boolean numeric = host.length() > 0;
        for (int i = 0; i < host.length(); i++) {
            char c = host.charAt(i);
            if (!((c >= '0' && c <= '9') || c == '.')) {
                numeric = false;
                break;
            }
        }
        return numeric ? null : host;
    }

    private String alpnCsv() {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < alpn.length; i++) {
            if (i > 0) sb.append(',');
            sb.append(alpn[i]);
        }
        return sb.toString();
    }

    private int minVersion() {
        boolean tls12 = false;
        for (int i = 0; i < protocols.length; i++) {
            if (protocols[i].equals("TLSv1.2") || protocols[i].equals("TLSv1.1") || protocols[i].equals("TLSv1")) {
                tls12 = true;
            }
        }
        return tls12 ? 12 : 13;
    }

    private String info(int which) {
        byte[] buf = new byte[512];
        int n = KuTls.info(handle, which, buf);
        return n > 0 ? new String(buf, 0, n) : "";
    }

    private X509Certificate[] peerChain() throws SSLException {
        byte[] buf = new byte[16384];
        int size = KuTls.peerChain(handle, buf);
        if (size > buf.length) {
            buf = new byte[size];
            size = KuTls.peerChain(handle, buf);
        }
        ArrayList<X509Certificate> certs = new ArrayList<X509Certificate>();
        int pos = 0;
        while (pos + 4 <= size) {
            int n = ((buf[pos] & 0xff) << 24) | ((buf[pos + 1] & 0xff) << 16)
                    | ((buf[pos + 2] & 0xff) << 8) | (buf[pos + 3] & 0xff);
            pos += 4;
            if (n < 0 || pos + n > size) break;
            byte[] der = new byte[n];
            System.arraycopy(buf, pos, der, 0, n);
            pos += n;
            try {
                certs.add(KuX509Certificate.parse(der));
            } catch (CertificateException e) {
                throw new SSLException("Unreadable server certificate: " + e.getMessage());
            }
        }
        return certs.toArray(new X509Certificate[certs.size()]);
    }

    /** The key exchange family the trust manager is told about. */
    private static String authType(String cipher) {
        if (cipher.indexOf("ECDHE_ECDSA") >= 0) return "ECDHE_ECDSA";
        if (cipher.indexOf("ECDHE_RSA") >= 0) return "ECDHE_RSA";
        if (cipher.indexOf("ECDSA") >= 0) return "ECDSA";
        return "RSA";
    }

    private void destroyHandle() {
        long h = handle;
        handle = 0;
        if (h != 0) KuTls.destroy(h);
    }

    // ── streams ──────────────────────────────────────────────────────────────

    @Override
    public InputStream getInputStream() throws IOException {
        if (closed) throw new SocketException("Socket is closed");
        if (!under.isConnected()) throw new SocketException("Socket is not connected");
        synchronized (lock) {
            if (in == null) in = new TlsInputStream();
            return in;
        }
    }

    @Override
    public OutputStream getOutputStream() throws IOException {
        if (closed) throw new SocketException("Socket is closed");
        if (!under.isConnected()) throw new SocketException("Socket is not connected");
        synchronized (lock) {
            if (out == null) out = new TlsOutputStream();
            return out;
        }
    }

    private final class TlsInputStream extends InputStream {
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
            startHandshake();
            long h = handle;
            if (closed || h == 0) throw new SocketException("Socket closed");
            int n = KuTls.read(h, b, off, len, under.getSoTimeout());
            if (n > 0) return n;
            if (n == 0) return -1;
            if (closed) throw new SocketException("Socket closed");
            if (n == -KuNet.ETIMEDOUT) throw new SocketTimeoutException("Read timed out");
            if (n == -KuNet.ECONNRESET) throw new SocketException("Connection reset");
            throw new SSLException("Read error: " + info(KuTls.INFO_ERROR) + " (" + n + ")");
        }

        @Override
        public int available() throws IOException {
            long h = handle;
            return closed || h == 0 ? 0 : KuTls.available(h);
        }

        @Override
        public void close() throws IOException {
            KuSSLSocket.this.close();
        }
    }

    private final class TlsOutputStream extends OutputStream {
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
            startHandshake();
            long h = handle;
            if (closed || h == 0) throw new SocketException("Socket closed");
            int n = KuTls.write(h, b, off, len, 0);
            if (n < 0) {
                if (closed) throw new SocketException("Socket closed");
                if (n == -KuNet.EPIPE) throw new SocketException("Broken pipe");
                if (n == -KuNet.ECONNRESET) throw new SocketException("Connection reset");
                throw new SSLException("Write error: " + info(KuTls.INFO_ERROR) + " (" + n + ")");
            }
        }

        @Override
        public void flush() {}

        @Override
        public void close() throws IOException {
            KuSSLSocket.this.close();
        }
    }

    // ── lifecycle ────────────────────────────────────────────────────────────

    @Override
    public void close() throws IOException {
        long h;
        synchronized (lock) {
            if (closed) return;
            closed = true;
            h = handle;
        }
        if (h != 0) {
            KuTls.shutdown(h);                       // close_notify, best effort
            int fd = under.kuFd();
            if (fd >= 0) KuNet.shutdown(fd, KuNet.SHUT_RDWR);  // wakes a reader blocked in the session
            synchronized (lock) {
                destroyHandle();
            }
        }
        if (autoClose) under.close();
    }

    // ── session and parameters ───────────────────────────────────────────────

    @Override
    public SSLSession getSession() {
        try {
            startHandshake();
        } catch (IOException e) {
            return new KuSSLSession(host, port, "NONE", "SSL_NULL_WITH_NULL_NULL", null);
        }
        return session;
    }

    @Override public SSLSession getHandshakeSession() { return handshaken ? session : null; }

    @Override
    public void addHandshakeCompletedListener(HandshakeCompletedListener listener) {
        if (listener == null) throw new IllegalArgumentException("listener == null");
        synchronized (listeners) {
            listeners.add(listener);
        }
    }

    @Override
    public void removeHandshakeCompletedListener(HandshakeCompletedListener listener) {
        synchronized (listeners) {
            listeners.remove(listener);
        }
    }

    @Override public String[] getSupportedCipherSuites() { return KuSSLSocketFactory.DEFAULT_SUITES.clone(); }
    @Override public String[] getEnabledCipherSuites() { return suites.clone(); }

    @Override
    public void setEnabledCipherSuites(String[] s) {
        if (s == null) throw new IllegalArgumentException("suites == null");
        suites = s.clone();
    }

    @Override public String[] getSupportedProtocols() { return KuSSLSocketFactory.DEFAULT_PROTOCOLS.clone(); }
    @Override public String[] getEnabledProtocols() { return protocols.clone(); }

    @Override
    public void setEnabledProtocols(String[] p) {
        if (p == null) throw new IllegalArgumentException("protocols == null");
        protocols = p.clone();
    }

    @Override
    public SSLParameters getSSLParameters() {
        SSLParameters p = new SSLParameters(suites, protocols);
        p.setApplicationProtocols(alpn);
        return p;
    }

    @Override
    public void setSSLParameters(SSLParameters params) {
        if (params == null) return;
        if (params.getCipherSuites() != null) suites = params.getCipherSuites();
        if (params.getProtocols() != null) protocols = params.getProtocols();
        alpn = params.getApplicationProtocols();
    }

    @Override
    public String getApplicationProtocol() {
        return negotiatedAlpn != null && negotiatedAlpn.length() > 0 ? negotiatedAlpn : null;
    }

    @Override
    public void setHostname(String hostname) { this.host = hostname; }

    // ── everything else belongs to the wrapped socket ────────────────────────

    @Override
    public void connect(SocketAddress endpoint) throws IOException { connect(endpoint, 0); }

    @Override
    public void connect(SocketAddress endpoint, int timeout) throws IOException {
        under.connect(endpoint, timeout);
        if (host == null && endpoint instanceof InetSocketAddress) {
            InetSocketAddress isa = (InetSocketAddress) endpoint;
            host = isa.getHostString();
            port = isa.getPort();
        }
    }

    @Override public void bind(SocketAddress bindpoint) throws IOException { under.bind(bindpoint); }
    @Override public InetAddress getInetAddress() { return under.getInetAddress(); }
    @Override public InetAddress getLocalAddress() { return under.getLocalAddress(); }
    @Override public int getPort() { return under.getPort(); }
    @Override public int getLocalPort() { return under.getLocalPort(); }
    @Override public SocketAddress getRemoteSocketAddress() { return under.getRemoteSocketAddress(); }
    @Override public SocketAddress getLocalSocketAddress() { return under.getLocalSocketAddress(); }
    @Override public FileDescriptor getFileDescriptor$() { return under.getFileDescriptor$(); }
    @Override public int kuFd() { return under.kuFd(); }
    @Override public boolean kuPeerClosed() { return closed || under.kuPeerClosed(); }
    @Override public void setTcpNoDelay(boolean on) throws SocketException { under.setTcpNoDelay(on); }
    @Override public boolean getTcpNoDelay() throws SocketException { return under.getTcpNoDelay(); }
    @Override public void setSoLinger(boolean on, int linger) throws SocketException { under.setSoLinger(on, linger); }
    @Override public int getSoLinger() throws SocketException { return under.getSoLinger(); }
    @Override public void setSoTimeout(int timeout) throws SocketException { under.setSoTimeout(timeout); }
    @Override public int getSoTimeout() throws SocketException { return under.getSoTimeout(); }
    @Override public void setSendBufferSize(int size) throws SocketException { under.setSendBufferSize(size); }
    @Override public int getSendBufferSize() throws SocketException { return under.getSendBufferSize(); }
    @Override public void setReceiveBufferSize(int size) throws SocketException { under.setReceiveBufferSize(size); }
    @Override public int getReceiveBufferSize() throws SocketException { return under.getReceiveBufferSize(); }
    @Override public void setKeepAlive(boolean on) throws SocketException { under.setKeepAlive(on); }
    @Override public boolean getKeepAlive() throws SocketException { return under.getKeepAlive(); }
    @Override public void setTrafficClass(int tc) throws SocketException { under.setTrafficClass(tc); }
    @Override public int getTrafficClass() throws SocketException { return under.getTrafficClass(); }
    @Override public void setReuseAddress(boolean on) throws SocketException { under.setReuseAddress(on); }
    @Override public boolean getReuseAddress() throws SocketException { return under.getReuseAddress(); }
    @Override public void setOOBInline(boolean on) throws SocketException { under.setOOBInline(on); }
    @Override public boolean getOOBInline() throws SocketException { return under.getOOBInline(); }
    @Override public boolean isConnected() { return under.isConnected(); }
    @Override public boolean isBound() { return under.isBound(); }
    @Override public boolean isClosed() { return closed || under.isClosed(); }
    @Override public boolean isInputShutdown() { return closed; }
    @Override public boolean isOutputShutdown() { return closed; }

    @Override
    public void shutdownInput() throws IOException {
        throw new UnsupportedOperationException("shutdownInput() not supported for SSL");
    }

    @Override
    public void shutdownOutput() throws IOException {
        throw new UnsupportedOperationException("shutdownOutput() not supported for SSL");
    }

    @Override
    public String toString() { return "KuSSLSocket[" + under + "]"; }
}
