package javax.net.ssl;

import java.security.KeyManagementException;
import java.security.NoSuchAlgorithmException;
import java.security.NoSuchProviderException;
import java.security.Provider;
import java.security.SecureRandom;

public class SSLContext {
    private final String protocol;

    protected SSLContext(String protocol) {
        this.protocol = protocol;
    }
    public static SSLContext getInstance(String protocol) throws NoSuchAlgorithmException {
        return new SSLContext(protocol);
    }
    public static SSLContext getInstance(String protocol, String provider) throws NoSuchAlgorithmException, NoSuchProviderException {
        return new SSLContext(protocol);
    }
    public static SSLContext getInstance(String protocol, Provider provider) throws NoSuchAlgorithmException {
        return new SSLContext(protocol);
    }
    public static SSLContext getDefault() throws NoSuchAlgorithmException {
        return new SSLContext("TLS");
    }
    public static void setDefault(SSLContext context) {}
    public final String getProtocol() { return protocol; }
    public final void init(KeyManager[] km, TrustManager[] tm, SecureRandom random) throws KeyManagementException {}
    public final SSLSocketFactory getSocketFactory() {
        return (SSLSocketFactory) SSLSocketFactory.getDefault();
    }
    public final SSLSessionContext getClientSessionContext() { return null; }
    public final SSLSessionContext getServerSessionContext() { return null; }
}
