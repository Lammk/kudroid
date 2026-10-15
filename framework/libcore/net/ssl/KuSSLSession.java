package libcore.net.ssl;

import java.security.Principal;
import java.security.cert.Certificate;
import java.security.cert.X509Certificate;
import java.util.HashMap;
import java.util.Map;
import javax.net.ssl.SSLPeerUnverifiedException;
import javax.net.ssl.SSLSession;
import javax.net.ssl.SSLSessionContext;

/** The outcome of a completed handshake. */
public final class KuSSLSession implements SSLSession {
    private final String host;
    private final int port;
    private final String protocol;
    private final String cipherSuite;
    private final X509Certificate[] peerCerts;
    private final long created = System.currentTimeMillis();
    private final byte[] id;
    private boolean valid = true;
    private final Map<String, Object> values = new HashMap<String, Object>();

    KuSSLSession(String host, int port, String protocol, String cipherSuite, X509Certificate[] peerCerts) {
        this.host = host;
        this.port = port;
        this.protocol = protocol;
        this.cipherSuite = cipherSuite;
        this.peerCerts = peerCerts;
        this.id = new byte[32];
        long t = created;
        for (int i = 0; i < id.length; i++) {
            t = t * 6364136223846793005L + 1442695040888963407L;
            id[i] = (byte) (t >>> 56);
        }
    }

    public byte[] getId() { return id.clone(); }
    public SSLSessionContext getSessionContext() { return null; }
    public long getCreationTime() { return created; }
    public long getLastAccessedTime() { return created; }
    public void invalidate() { valid = false; }
    public boolean isValid() { return valid; }
    public void putValue(String name, Object value) { values.put(name, value); }
    public Object getValue(String name) { return values.get(name); }
    public void removeValue(String name) { values.remove(name); }

    public String[] getValueNames() {
        return values.keySet().toArray(new String[values.size()]);
    }

    public Certificate[] getPeerCertificates() throws SSLPeerUnverifiedException {
        if (peerCerts == null || peerCerts.length == 0) {
            throw new SSLPeerUnverifiedException("peer not authenticated");
        }
        return peerCerts.clone();
    }

    public Certificate[] getLocalCertificates() { return null; }

    public Principal getPeerPrincipal() throws SSLPeerUnverifiedException {
        return ((X509Certificate) getPeerCertificates()[0]).getSubjectX500Principal();
    }

    public Principal getLocalPrincipal() { return null; }
    public String getCipherSuite() { return cipherSuite; }
    public String getProtocol() { return protocol; }
    public String getPeerHost() { return host; }
    public int getPeerPort() { return port; }
}
