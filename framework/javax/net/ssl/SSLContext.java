package javax.net.ssl;

import java.security.KeyManagementException;
import java.security.NoSuchAlgorithmException;
import java.security.SecureRandom;
import libcore.net.ssl.KuSSLSocketFactory;

public class SSLContext {
    private static SSLContext sDefault;

    private final String protocol;
    private X509TrustManager trustManager;

    protected SSLContext(String protocol) {
        this.protocol = protocol;
    }

    public static SSLContext getInstance(String protocol) throws NoSuchAlgorithmException {
        if (protocol == null) throw new NullPointerException("protocol == null");
        return new SSLContext(protocol);
    }

    public static SSLContext getInstance(String protocol, String provider) throws NoSuchAlgorithmException {
        return getInstance(protocol);
    }

    public static synchronized SSLContext getDefault() throws NoSuchAlgorithmException {
        if (sDefault == null) sDefault = new SSLContext("Default");
        return sDefault;
    }

    public static synchronized void setDefault(SSLContext context) {
        if (context == null) throw new NullPointerException("context == null");
        sDefault = context;
    }

    public void init(KeyManager[] km, TrustManager[] tm, SecureRandom random) throws KeyManagementException {
        trustManager = null;
        if (tm != null) {
            for (int i = 0; i < tm.length; i++) {
                if (tm[i] instanceof X509TrustManager) {
                    trustManager = (X509TrustManager) tm[i];
                    break;
                }
            }
        }
    }

    public SSLSocketFactory getSocketFactory() {
        return new KuSSLSocketFactory(trustManager);
    }

    public SSLSessionContext getServerSessionContext() { return null; }
    public SSLSessionContext getClientSessionContext() { return null; }
    public String getProtocol() { return protocol; }

    public SSLParameters getDefaultSSLParameters() {
        return new SSLParameters(KuSSLSocketFactory.DEFAULT_SUITES, KuSSLSocketFactory.DEFAULT_PROTOCOLS);
    }

    public SSLParameters getSupportedSSLParameters() {
        return new SSLParameters(KuSSLSocketFactory.DEFAULT_SUITES, KuSSLSocketFactory.DEFAULT_PROTOCOLS);
    }
}
