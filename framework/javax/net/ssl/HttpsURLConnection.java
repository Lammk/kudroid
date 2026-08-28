package javax.net.ssl;

import java.net.HttpURLConnection;
import java.net.URL;
import java.security.Principal;
import java.security.cert.Certificate;

public abstract class HttpsURLConnection extends HttpURLConnection {
    protected HostnameVerifier hostnameVerifier;
    private SSLSocketFactory sslSocketFactory = (SSLSocketFactory) SSLSocketFactory.getDefault();

    protected HttpsURLConnection(URL url) {
        super(url);
    }
    public abstract String getCipherSuite();
    public abstract Certificate[] getLocalCertificates();
    public abstract Certificate[] getServerCertificates() throws SSLPeerUnverifiedException;
    public Principal getPeerPrincipal() throws SSLPeerUnverifiedException { return null; }
    public Principal getLocalPrincipal() { return null; }
    public void setHostnameVerifier(HostnameVerifier v) { this.hostnameVerifier = v; }
    public HostnameVerifier getHostnameVerifier() { return hostnameVerifier; }
    public static void setDefaultHostnameVerifier(HostnameVerifier v) {}
    public static HostnameVerifier getDefaultHostnameVerifier() {
        return new HostnameVerifier() {
            public boolean verify(String hostname, SSLSession session) { return true; }
        };
    }
    public void setSSLSocketFactory(SSLSocketFactory sf) { this.sslSocketFactory = sf; }
    public SSLSocketFactory getSSLSocketFactory() { return sslSocketFactory; }
    public static void setDefaultSSLSocketFactory(SSLSocketFactory sf) {}
    public static SSLSocketFactory getDefaultSSLSocketFactory() {
        return (SSLSocketFactory) SSLSocketFactory.getDefault();
    }
}
