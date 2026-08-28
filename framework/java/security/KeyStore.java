package java.security;

import java.io.InputStream;
import java.io.OutputStream;
import java.io.IOException;
import java.security.cert.Certificate;
import java.security.cert.CertificateException;
import java.util.Enumeration;
import java.util.Collections;

public class KeyStore {
    private final String type;

    protected KeyStore(String type) { this.type = type; }
    public static KeyStore getInstance(String type) throws KeyStoreException { return new KeyStore(type); }
    public static String getDefaultType() { return "PKCS12"; }
    public String getType() { return type; }
    public void load(InputStream stream, char[] password) throws IOException, NoSuchAlgorithmException, CertificateException {}
    public void store(OutputStream stream, char[] password) throws IOException, NoSuchAlgorithmException, CertificateException {}
    public Key getKey(String alias, char[] password) throws KeyStoreException, NoSuchAlgorithmException, UnrecoverableKeyException { return null; }
    public Certificate[] getCertificateChain(String alias) throws KeyStoreException { return null; }
    public Certificate getCertificate(String alias) throws KeyStoreException { return null; }
    public void setKeyEntry(String alias, Key key, char[] password, Certificate[] chain) throws KeyStoreException {}
    public void setCertificateEntry(String alias, Certificate cert) throws KeyStoreException {}
    public void deleteEntry(String alias) throws KeyStoreException {}
    public Enumeration<String> aliases() throws KeyStoreException { return Collections.emptyEnumeration(); }
    public boolean containsAlias(String alias) throws KeyStoreException { return false; }
    public int size() throws KeyStoreException { return 0; }
    public boolean isKeyEntry(String alias) throws KeyStoreException { return false; }
    public boolean isCertificateEntry(String alias) throws KeyStoreException { return false; }
}
