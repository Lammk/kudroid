package javax.net.ssl;

import java.security.NoSuchAlgorithmException;
import java.security.KeyManagementException;
import java.security.SecureRandom;

public class SSLContext {
    private final String protocol;

    protected SSLContext(String protocol) {
        this.protocol = protocol;
    }
    public static SSLContext getInstance(String protocol) throws NoSuchAlgorithmException {
        return new SSLContext(protocol);
    }
    public static SSLContext getDefault() throws NoSuchAlgorithmException {
        return new SSLContext("Default");
    }
    public static void setDefault(SSLContext context) {}
    public void init(KeyManager[] km, TrustManager[] tm, SecureRandom random) throws KeyManagementException {}
    public SSLSocketFactory getSocketFactory() {
        return (SSLSocketFactory) SSLSocketFactory.getDefault();
    }
    public SSLSessionContext getServerSessionContext() { return null; }
    public SSLSessionContext getClientSessionContext() { return null; }
    public String getProtocol() { return protocol; }
}
