package javax.net.ssl;

import java.security.Principal;
import java.security.cert.Certificate;

public interface SSLSession {
    byte[] getId();
    SSLSessionContext getSessionContext();
    long getCreationTime();
    long getLastAccessedTime();
    void invalidate();
    boolean isValid();
    void putValue(String name, Object value);
    Object getValue(String name);
    void removeValue(String name);
    String[] getValueNames();
    Certificate[] getPeerCertificates() throws SSLPeerUnverifiedException;
    Certificate[] getLocalCertificates();
    Principal getPeerPrincipal() throws SSLPeerUnverifiedException;
    Principal getLocalPrincipal();
    String getCipherSuite();
    String getProtocol();
    String getPeerHost();
    int getPeerPort();
}
