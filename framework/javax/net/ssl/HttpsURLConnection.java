package javax.net.ssl;

import java.net.HttpURLConnection;
import java.net.URL;
import java.security.cert.Certificate;
import java.security.Principal;

public abstract class HttpsURLConnection extends HttpURLConnection {
    protected HostnameVerifier hostnameVerifier;
    private static HostnameVerifier defaultHostnameVerifier = new HostnameVerifier() {
        public boolean verify(String hostname, SSLSession session) { return true; }
    };
    private static SSLSocketFactory defaultSSLSocketFactory = (SSLSocketFactory) SSLSocketFactory.getDefault();
    private SSLSocketFactory sslSocketFactory = defaultSSLSocketFactory;

    protected HttpsURLConnection(URL url) { super(url); }
    public abstract String getCipherSuite();
    public abstract Certificate[] getLocalCertificates();
    public abstract Certificate[] getServerCertificates() throws SSLPeerUnverifiedException;
    public Principal getPeerPrincipal() throws SSLPeerUnverifiedException { return null; }
    public Principal getLocalPrincipal() { return null; }
    public static void setDefaultHostnameVerifier(HostnameVerifier v) { defaultHostnameVerifier = v; }
    public static HostnameVerifier getDefaultHostnameVerifier() { return defaultHostnameVerifier; }
    public void setHostnameVerifier(HostnameVerifier v) { this.hostnameVerifier = v; }
    public HostnameVerifier getHostnameVerifier() { return hostnameVerifier != null ? hostnameVerifier : defaultHostnameVerifier; }
    public static void setDefaultSSLSocketFactory(SSLSocketFactory sf) { defaultSSLSocketFactory = sf; }
    public static SSLSocketFactory getDefaultSSLSocketFactory() { return defaultSSLSocketFactory; }
    public void setSSLSocketFactory(SSLSocketFactory sf) { this.sslSocketFactory = sf; }
    public SSLSocketFactory getSSLSocketFactory() { return sslSocketFactory; }
}
