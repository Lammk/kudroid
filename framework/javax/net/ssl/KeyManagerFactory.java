package javax.net.ssl;

import java.security.KeyStore;
import java.security.KeyStoreException;
import java.security.NoSuchAlgorithmException;
import java.security.NoSuchProviderException;
import java.security.UnrecoverableKeyException;
import java.security.Provider;

public class KeyManagerFactory {
    private final String algorithm;

    protected KeyManagerFactory(String algorithm) {
        this.algorithm = algorithm;
    }
    public static final String getDefaultAlgorithm() { return "PKIX"; }
    public final String getAlgorithm() { return algorithm; }
    public static KeyManagerFactory getInstance(String algorithm) throws NoSuchAlgorithmException {
        return new KeyManagerFactory(algorithm);
    }
    public static KeyManagerFactory getInstance(String algorithm, String provider) throws NoSuchAlgorithmException, NoSuchProviderException {
        return new KeyManagerFactory(algorithm);
    }
    public static KeyManagerFactory getInstance(String algorithm, Provider provider) throws NoSuchAlgorithmException {
        return new KeyManagerFactory(algorithm);
    }
    public final void init(KeyStore ks, char[] password) throws KeyStoreException, NoSuchAlgorithmException, UnrecoverableKeyException {}
    public final KeyManager[] getKeyManagers() { return new KeyManager[0]; }
}
