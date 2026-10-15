package libcore.net.ssl;

import java.io.IOException;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.net.UnknownHostException;
import javax.net.ssl.SSLSocketFactory;
import javax.net.ssl.X509TrustManager;

/** Creates TLS client sockets that judge the server with one trust manager. */
public final class KuSSLSocketFactory extends SSLSocketFactory {
    public static final String[] DEFAULT_PROTOCOLS = {"TLSv1.3", "TLSv1.2"};
    public static final String[] DEFAULT_SUITES = {
        "TLS_AES_128_GCM_SHA256",
        "TLS_AES_256_GCM_SHA384",
        "TLS_CHACHA20_POLY1305_SHA256",
        "TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256",
        "TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256",
        "TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384",
        "TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384",
        "TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256",
        "TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256",
    };

    private final X509TrustManager trustManager;

    /** A null trust manager means the platform's. */
    public KuSSLSocketFactory(X509TrustManager trustManager) {
        this.trustManager = trustManager != null ? trustManager : KuTrustManager.platform();
    }

    @Override public String[] getDefaultCipherSuites() { return DEFAULT_SUITES.clone(); }
    @Override public String[] getSupportedCipherSuites() { return DEFAULT_SUITES.clone(); }

    @Override
    public Socket createSocket() throws IOException {
        return new KuSSLSocket(new Socket(), null, -1, true, trustManager);
    }

    @Override
    public Socket createSocket(Socket s, String host, int port, boolean autoClose) throws IOException {
        if (s == null) throw new NullPointerException("socket == null");
        return new KuSSLSocket(s, host, port, autoClose, trustManager);
    }

    @Override
    public Socket createSocket(String host, int port) throws IOException, UnknownHostException {
        Socket plain = new Socket(host, port);
        return new KuSSLSocket(plain, host, port, true, trustManager);
    }

    @Override
    public Socket createSocket(String host, int port, InetAddress localHost, int localPort)
            throws IOException, UnknownHostException {
        Socket plain = new Socket(host, port, localHost, localPort);
        return new KuSSLSocket(plain, host, port, true, trustManager);
    }

    @Override
    public Socket createSocket(InetAddress host, int port) throws IOException {
        Socket plain = new Socket(host, port);
        return new KuSSLSocket(plain, host.getHostName(), port, true, trustManager);
    }

    @Override
    public Socket createSocket(InetAddress address, int port, InetAddress localAddress, int localPort)
            throws IOException {
        Socket plain = new Socket(address, port, localAddress, localPort);
        return new KuSSLSocket(plain, address.getHostName(), port, true, trustManager);
    }
}
