package javax.net.ssl;

import java.security.KeyStore;
import java.security.KeyStoreException;
import java.security.NoSuchAlgorithmException;
import java.security.NoSuchProviderException;
import java.security.Provider;
import java.security.cert.Certificate;
import java.security.cert.CertificateException;
import java.security.cert.X509Certificate;
import java.util.ArrayList;
import java.util.Enumeration;
import libcore.net.ssl.KuTrustManager;

public class TrustManagerFactory {
    private final String algorithm;
    private TrustManager[] managers;

    protected TrustManagerFactory(String algorithm) {
        this.algorithm = algorithm;
    }

    public static final String getDefaultAlgorithm() { return "PKIX"; }
    public final String getAlgorithm() { return algorithm; }

    public static TrustManagerFactory getInstance(String algorithm) throws NoSuchAlgorithmException {
        return new TrustManagerFactory(algorithm);
    }

    public static TrustManagerFactory getInstance(String algorithm, String provider)
            throws NoSuchAlgorithmException, NoSuchProviderException {
        return new TrustManagerFactory(algorithm);
    }

    public static TrustManagerFactory getInstance(String algorithm, Provider provider)
            throws NoSuchAlgorithmException {
        return new TrustManagerFactory(algorithm);
    }

    /** With no key store the platform's roots are trusted; with one, only its certificates. */
    public final void init(KeyStore ks) throws KeyStoreException {
        if (ks == null) {
            managers = new TrustManager[] {KuTrustManager.platform()};
            return;
        }
        ArrayList<X509Certificate> roots = new ArrayList<X509Certificate>();
        Enumeration<String> aliases = ks.aliases();
        while (aliases.hasMoreElements()) {
            Certificate c = ks.getCertificate(aliases.nextElement());
            if (c instanceof X509Certificate) roots.add((X509Certificate) c);
        }
        try {
            managers = new TrustManager[] {KuTrustManager.forAnchors(roots)};
        } catch (CertificateException e) {
            throw new KeyStoreException(e.getMessage());
        }
    }

    public final void init(ManagerFactoryParameters spec) {
        managers = new TrustManager[] {KuTrustManager.platform()};
    }

    public final TrustManager[] getTrustManagers() {
        if (managers == null) throw new IllegalStateException("TrustManagerFactory is not initialized");
        return managers.clone();
    }
}
