import java.io.ByteArrayInputStream;
import java.io.InputStream;
import java.net.URL;
import java.security.KeyStore;
import java.security.cert.CertificateFactory;
import java.security.cert.X509Certificate;
import javax.net.ssl.HostnameVerifier;
import javax.net.ssl.HttpsURLConnection;
import javax.net.ssl.SSLContext;
import javax.net.ssl.SSLException;
import javax.net.ssl.SSLHandshakeException;
import javax.net.ssl.SSLPeerUnverifiedException;
import javax.net.ssl.SSLSession;
import javax.net.ssl.SSLSocket;
import javax.net.ssl.SSLSocketFactory;
import javax.net.ssl.TrustManager;
import javax.net.ssl.TrustManagerFactory;
import javax.net.ssl.X509TrustManager;

public final class TlsTests {
    private static void check(boolean ok, String name) { TestSupport.check(ok, name); }

    public static String run(int good, int self, int mismatch, String caPem, String selfPem) {
        TestSupport.report.setLength(0);
        try { go(good, self, mismatch, caPem, selfPem); } catch (Throwable t) { TestSupport.fail("tls", t); }
        return TestSupport.report.toString();
    }

    private static HttpsURLConnection open(String url) throws Exception {
        return (HttpsURLConnection) new URL(url).openConnection();
    }

    private static String fetch(HttpsURLConnection c) throws Exception {
        return TestSupport.read(c.getInputStream());
    }

    static final class TrustAll implements X509TrustManager {
        public void checkClientTrusted(X509Certificate[] chain, String authType) {}
        public void checkServerTrusted(X509Certificate[] chain, String authType) {}
        public X509Certificate[] getAcceptedIssuers() { return new X509Certificate[0]; }
    }

    static X509Certificate cert(String pem) throws Exception {
        return (X509Certificate) CertificateFactory.getInstance("X.509")
                .generateCertificate(new ByteArrayInputStream(pem.getBytes()));
    }

    static SSLSocketFactory factoryTrusting(String pem) throws Exception {
        KeyStore ks = KeyStore.getInstance(KeyStore.getDefaultType());
        ks.load(null, null);
        ks.setCertificateEntry("anchor", cert(pem));
        TrustManagerFactory tmf = TrustManagerFactory.getInstance(TrustManagerFactory.getDefaultAlgorithm());
        tmf.init(ks);
        SSLContext ctx = SSLContext.getInstance("TLS");
        ctx.init(null, tmf.getTrustManagers(), null);
        return ctx.getSocketFactory();
    }

    static void go(int good, int self, int mismatch, String caPem, String selfPem) throws Exception {
        // The platform roots include the test CA, so the default stack trusts the good server.
        HttpsURLConnection c = open("https://localhost:" + good + "/hello");
        check(fetch(c).equals("hello world"), "https GET through the default trust manager");
        check(c.getCipherSuite() != null && c.getCipherSuite().startsWith("TLS_"), "negotiated cipher suite");
        check(c.getServerCertificates().length >= 1, "server certificates exposed");
        X509Certificate leaf = (X509Certificate) c.getServerCertificates()[0];
        check(leaf.getSubjectX500Principal().getName().indexOf("CN=localhost") >= 0, "leaf subject parsed");
        check(leaf.getSubjectAlternativeNames() != null && leaf.getSubjectAlternativeNames().size() >= 2, "leaf SANs parsed");
        check(leaf.getNotAfter().getTime() > System.currentTimeMillis(), "leaf validity parsed");
        check(leaf.getPublicKey().getEncoded().length > 20, "public key encoding kept");

        // Same connection again goes through the pooled TLS connection.
        check(fetch(open("https://localhost:" + good + "/hello")).equals("hello world"), "pooled TLS connection reused");

        try {
            fetch(open("https://localhost:" + self + "/hello"));
            check(false, "self-signed server rejected");
        } catch (SSLHandshakeException e) {
            check(true, "self-signed server rejected");
        } catch (SSLException e) {
            check(true, "self-signed server rejected (" + e + ")");
        }

        HttpsURLConnection all = open("https://localhost:" + self + "/hello");
        SSLContext trustAll = SSLContext.getInstance("TLS");
        trustAll.init(null, new TrustManager[] {new TrustAll()}, null);
        all.setSSLSocketFactory(trustAll.getSocketFactory());
        check(fetch(all).equals("hello world"), "custom trust manager accepts a self-signed server");

        HttpsURLConnection anchored = open("https://localhost:" + self + "/hello");
        anchored.setSSLSocketFactory(factoryTrusting(selfPem));
        check(fetch(anchored).equals("hello world"), "key store anchor trusts the self-signed server");

        HttpsURLConnection wrongAnchor = open("https://localhost:" + self + "/hello");
        wrongAnchor.setSSLSocketFactory(factoryTrusting(caPem));
        try {
            fetch(wrongAnchor);
            check(false, "wrong anchor rejects the server");
        } catch (SSLException e) {
            check(true, "wrong anchor rejects the server");
        }

        try {
            fetch(open("https://localhost:" + mismatch + "/hello"));
            check(false, "host name mismatch rejected");
        } catch (SSLPeerUnverifiedException e) {
            check(true, "host name mismatch rejected");
        }
        HttpsURLConnection lenient = open("https://localhost:" + mismatch + "/hello");
        lenient.setHostnameVerifier(new HostnameVerifier() {
            public boolean verify(String hostname, SSLSession session) { return true; }
        });
        check(fetch(lenient).equals("hello world"), "custom host name verifier accepts the mismatch");

        // The IP address is in the good certificate's SANs.
        check(fetch(open("https://127.0.0.1:" + good + "/hello")).equals("hello world"), "IP SAN matches an address host");

        // A raw SSLSocket speaks TLS on its own.
        SSLSocket s = (SSLSocket) ((SSLSocketFactory) SSLSocketFactory.getDefault()).createSocket("localhost", good);
        s.getOutputStream().write("GET /hello HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n".getBytes());
        String reply = TestSupport.read(s.getInputStream());
        check(reply.startsWith("HTTP/1.1 200") && reply.endsWith("hello world"), "SSLSocket request/response");
        check(s.getSession().getProtocol().startsWith("TLSv1"), "session protocol");
        s.close();
        check(s.isClosed(), "SSLSocket closes");
    }
}
