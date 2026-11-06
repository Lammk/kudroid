import android.net.ssl.SSLSockets;
import java.io.InputStream;
import java.net.InetSocketAddress;
import javax.net.ssl.SSLContext;
import javax.net.ssl.SSLParameters;
import javax.net.ssl.SSLSession;
import javax.net.ssl.SSLSocket;
import javax.net.ssl.TrustManager;
import javax.net.ssl.X509TrustManager;
import java.security.cert.X509Certificate;

public final class AndroidTlsTests {
    private static int checks;
    private static int trustChecks;

    private static void check(boolean ok, String name) {
        if (!ok) throw new AssertionError(name);
        ++checks;
        System.out.println("PASS " + name);
    }

    private static final class ForeignSocket extends SSLSocket {
        public String[] getSupportedCipherSuites() { return new String[0]; }
        public String[] getEnabledCipherSuites() { return new String[0]; }
        public void setEnabledCipherSuites(String[] value) {}
        public String[] getSupportedProtocols() { return new String[0]; }
        public String[] getEnabledProtocols() { return new String[0]; }
        public void setEnabledProtocols(String[] value) {}
        public SSLSession getSession() { return null; }
        public void startHandshake() {}
    }

    private static void exchange(SSLContext context, int port, boolean tickets) throws Exception {
        SSLSocket socket = (SSLSocket) context.getSocketFactory().createSocket();
        try {
            check(SSLSockets.isSupportedSocket(socket), "platform socket is supported");
            SSLSockets.setUseSessionTickets(socket, !tickets);
            SSLSockets.setUseSessionTickets(socket, tickets);
            SSLParameters params = socket.getSSLParameters();
            params.setProtocols(new String[] {"TLSv1.2"});
            params.setApplicationProtocols(new String[] {"http/1.1"});
            socket.setSSLParameters(params);
            check("http/1.1".equals(socket.getSSLParameters().getApplicationProtocols()[0]),
                    "ALPN survives platform ticket configuration");
            socket.setSoTimeout(3000);
            socket.connect(new InetSocketAddress("127.0.0.1", port), 3000);
            socket.startHandshake();
            check("http/1.1".equals(socket.getApplicationProtocol()), "ALPN is negotiated");
            check("TLSv1.2".equals(socket.getSession().getProtocol()), "TLS session is established");
            SSLSockets.setUseSessionTickets(socket, !tickets);
            socket.getOutputStream().write("GET / HTTP/1.0\r\n\r\n".getBytes("US-ASCII"));
            InputStream in = socket.getInputStream();
            byte[] response = new byte[15];
            int offset = 0;
            while (offset < response.length) {
                int count = in.read(response, offset, response.length - offset);
                if (count < 0) break;
                offset += count;
            }
            check(offset == response.length && "HTTP/1.0 200 ok".equals(
                    new String(response, "US-ASCII")), "encrypted request and response complete");
        } finally {
            socket.close();
        }
    }

    public static int run(int port) throws Exception {
        checks = 0;
        trustChecks = 0;
        check(!SSLSockets.isSupportedSocket(null), "null socket is unsupported");
        SSLSocket foreign = new ForeignSocket();
        check(!SSLSockets.isSupportedSocket(foreign), "foreign provider is unsupported");
        try {
            SSLSockets.setUseSessionTickets(foreign, true);
            check(false, "foreign socket setter must reject");
        } catch (IllegalArgumentException expected) {
            check(true, "foreign socket setter rejects");
        }
        try {
            SSLSockets.setUseSessionTickets(null, false);
            check(false, "null socket setter must reject");
        } catch (IllegalArgumentException expected) {
            check(true, "null socket setter rejects");
        }
        X509TrustManager trust = new X509TrustManager() {
            public X509Certificate[] getAcceptedIssuers() { return new X509Certificate[0]; }
            public void checkClientTrusted(X509Certificate[] chain, String authType) {}
            public void checkServerTrusted(X509Certificate[] chain, String authType) {
                check(chain.length > 0, "handshake passes the peer chain to the trust manager");
                ++trustChecks;
            }
        };
        SSLContext context = SSLContext.getInstance("TLS");
        context.init(null, new TrustManager[] {trust}, null);
        exchange(context, port, false);
        exchange(context, port, true);
        check(trustChecks == 2, "both connections retain certificate validation");
        return checks;
    }
}
