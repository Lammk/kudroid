package javax.net.ssl;

import java.security.KeyStore;
import java.security.KeyStoreException;
import java.security.NoSuchAlgorithmException;
import java.security.NoSuchProviderException;
import java.security.Provider;

public class TrustManagerFactory {
    private final String algorithm;

    protected TrustManagerFactory(String algorithm) {
        this.algorithm = algorithm;
    }
    public static final String getDefaultAlgorithm() { return "PKIX"; }
    public final String getAlgorithm() { return algorithm; }
    public static TrustManagerFactory getInstance(String algorithm) throws NoSuchAlgorithmException {
        return new TrustManagerFactory(algorithm);
    }
    public static TrustManagerFactory getInstance(String algorithm, String provider) throws NoSuchAlgorithmException, NoSuchProviderException {
        return new TrustManagerFactory(algorithm);
    }
    public static TrustManagerFactory getInstance(String algorithm, Provider provider) throws NoSuchAlgorithmException {
        return new TrustManagerFactory(algorithm);
    }
    public final void init(KeyStore ks) throws KeyStoreException {}
    public final TrustManager[] getTrustManagers() {
        return new TrustManager[]{
            new X509TrustManager() {
                public void checkClientTrusted(java.security.cert.X509Certificate[] chain, String authType) {}
                public void checkServerTrusted(java.security.cert.X509Certificate[] chain, String authType) {}
                public java.security.cert.X509Certificate[] getAcceptedIssuers() { return new java.security.cert.X509Certificate[0]; }
            }
        };
    }
}
