package javax.net.ssl;

import java.io.IOException;
import java.net.Proxy;
import java.net.URL;
import java.security.Principal;
import java.security.cert.Certificate;

/** The platform's https: connection; the exchange itself lives in HttpURLConnection. */
public final class KuHttpsURLConnection extends HttpsURLConnection {
    private final Proxy proxy;

    public KuHttpsURLConnection(URL url, Proxy proxy) {
        super(url);
        this.proxy = proxy;
    }

    @Override protected SSLSocketFactory kuSslSocketFactory() { return getSSLSocketFactory(); }
    @Override protected HostnameVerifier kuHostnameVerifier() { return getHostnameVerifier(); }

    @Override
    public void disconnect() { kuDisconnect(); }

    @Override
    public boolean usingProxy() { return proxy != null && proxy.type() != Proxy.Type.DIRECT; }

    @Override
    public String getCipherSuite() {
        try {
            SSLSession s = kuSession();
            return s != null ? s.getCipherSuite() : null;
        } catch (IOException e) {
            return null;
        }
    }

    @Override
    public Certificate[] getLocalCertificates() { return null; }

    @Override
    public Certificate[] getServerCertificates() throws SSLPeerUnverifiedException {
        try {
            SSLSession s = kuSession();
            if (s == null) throw new IllegalStateException("connection not yet open");
            return s.getPeerCertificates();
        } catch (SSLPeerUnverifiedException e) {
            throw e;
        } catch (IOException e) {
            throw new IllegalStateException("connection not yet open");
        }
    }

    @Override
    public Principal getPeerPrincipal() throws SSLPeerUnverifiedException {
        try {
            SSLSession s = kuSession();
            if (s == null) throw new IllegalStateException("connection not yet open");
            return s.getPeerPrincipal();
        } catch (SSLPeerUnverifiedException e) {
            throw e;
        } catch (IOException e) {
            throw new IllegalStateException("connection not yet open");
        }
    }
}
