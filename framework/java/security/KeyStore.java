package java.security;

import java.io.InputStream;
import java.io.OutputStream;
import java.io.IOException;
import java.security.cert.Certificate;
import java.security.cert.CertificateException;
import java.util.Enumeration;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.Map;

/** Holds trusted certificate entries; private keys and the on-disk store formats are not supported. */
public class KeyStore {
    private final String type;
    private final Map<String, Certificate> certificates = new LinkedHashMap<String, Certificate>();

    protected KeyStore(String type) { this.type = type; }
    public static KeyStore getInstance(String type) throws KeyStoreException { return new KeyStore(type); }
    public static String getDefaultType() { return "PKCS12"; }
    public String getType() { return type; }
    public void load(InputStream stream, char[] password) throws IOException, NoSuchAlgorithmException, CertificateException {}
    public void store(OutputStream stream, char[] password) throws IOException, NoSuchAlgorithmException, CertificateException {}
    public Key getKey(String alias, char[] password) throws KeyStoreException, NoSuchAlgorithmException, UnrecoverableKeyException { return null; }
    public Certificate[] getCertificateChain(String alias) throws KeyStoreException { return null; }
    public Certificate getCertificate(String alias) throws KeyStoreException {
        synchronized (certificates) {
            return certificates.get(alias);
        }
    }
    public void setKeyEntry(String alias, Key key, char[] password, Certificate[] chain) throws KeyStoreException {
        if (chain != null && chain.length > 0) setCertificateEntry(alias, chain[0]);
    }
    public void setCertificateEntry(String alias, Certificate cert) throws KeyStoreException {
        if (alias == null) throw new NullPointerException("alias == null");
        synchronized (certificates) {
            if (cert == null) certificates.remove(alias); else certificates.put(alias, cert);
        }
    }
    public void deleteEntry(String alias) throws KeyStoreException {
        synchronized (certificates) {
            certificates.remove(alias);
        }
    }
    public Enumeration<String> aliases() throws KeyStoreException {
        synchronized (certificates) {
            return Collections.enumeration(new java.util.ArrayList<String>(certificates.keySet()));
        }
    }
    public boolean containsAlias(String alias) throws KeyStoreException {
        synchronized (certificates) {
            return certificates.containsKey(alias);
        }
    }
    public int size() throws KeyStoreException {
        synchronized (certificates) {
            return certificates.size();
        }
    }
    public boolean isKeyEntry(String alias) throws KeyStoreException { return false; }
    public boolean isCertificateEntry(String alias) throws KeyStoreException { return containsAlias(alias); }
}
