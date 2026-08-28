package java.security;

import java.io.InputStream;
import java.io.OutputStream;
import java.io.IOException;
import java.security.cert.Certificate;
import java.security.cert.CertificateException;
import java.util.Enumeration;
import java.util.Collections;
import java.util.Date;

public class KeyStore {
    private final String type;

    protected KeyStore(String type) {
        this.type = type;
    }
    public static KeyStore getInstance(String type) throws KeyStoreException {
        return new KeyStore(type);
    }
    public static KeyStore getInstance(String type, String provider) throws KeyStoreException, NoSuchProviderException {
        return new KeyStore(type);
    }
    public static KeyStore getInstance(String type, Provider provider) throws KeyStoreException {
        return new KeyStore(type);
    }
    public static final String getDefaultType() {
        return "BKS";
    }
    public final String getType() { return type; }

    public final Key getKey(String alias, char[] password) throws KeyStoreException, NoSuchAlgorithmException, UnrecoverableKeyException {
        return null;
    }
    public final Certificate[] getCertificateChain(String alias) throws KeyStoreException {
        return null;
    }
    public final Certificate getCertificate(String alias) throws KeyStoreException {
        return null;
    }
    public final Date getCreationDate(String alias) throws KeyStoreException {
        return new Date();
    }
    public final void setKeyEntry(String alias, Key key, char[] password, Certificate[] chain) throws KeyStoreException {}
    public final void setKeyEntry(String alias, byte[] key, Certificate[] chain) throws KeyStoreException {}
    public final void setCertificateEntry(String alias, Certificate cert) throws KeyStoreException {}
    public final void deleteEntry(String alias) throws KeyStoreException {}
    public final Enumeration<String> aliases() throws KeyStoreException {
        return Collections.<String>emptyEnumeration();
    }
    public final boolean containsAlias(String alias) throws KeyStoreException {
        return false;
    }
    public final int size() throws KeyStoreException { return 0; }
    public final boolean isKeyEntry(String alias) throws KeyStoreException { return false; }
    public final boolean isCertificateEntry(String alias) throws KeyStoreException { return false; }
    public final String getCertificateAlias(Certificate cert) throws KeyStoreException { return null; }
    public final void store(OutputStream stream, char[] password) throws KeyStoreException, IOException, NoSuchAlgorithmException, CertificateException {}
    public final void load(InputStream stream, char[] password) throws IOException, NoSuchAlgorithmException, CertificateException {}
}
