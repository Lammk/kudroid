package com.android.org.conscrypt;

/**
 * com.android.org.conscrypt.OpenSSLSocketImpl.
 *
 * This is the AOSP-internal Conscrypt socket, and its ONLY purpose here is to be
 * findable by name. okhttp's Android platform detection does
 * {@code Class.forName("com.android.org.conscrypt.OpenSSLSocketImpl")} to decide
 * which socket adapter to install; when the lookup failed, okhttp fell through to a
 * path that dereferenced a null companion and threw, leaving its Platform class
 * permanently in error — which kills every HTTP call for the rest of the process.
 *
 * Declared abstract with the methods okhttp reflects on. It is never instantiated:
 * KuDroid has no BoringSSL, and okhttp only uses the class to type-check a socket it
 * obtained elsewhere. An instantiable version claiming to do TLS would be worse,
 * because a caller would get a socket that silently fails to encrypt.
 *
 * The name is Android's, not ours; it is a compatibility surface the same way a
 * framework class is.
 */
public abstract class OpenSSLSocketImpl extends javax.net.ssl.SSLSocket {

    protected OpenSSLSocketImpl() {
    }

    /**
     * ALPN protocols, reflected on by okhttp to negotiate HTTP/2.
     *
     * KuDroid does not implement the handshake, so there is nothing to configure.
     */
    public void setAlpnProtocols(byte[] alpnProtocols) {
    }

    public byte[] getAlpnSelectedProtocol() {
        return null;
    }

    public void setUseSessionTickets(boolean useSessionTickets) {
    }

    public void setHostname(String hostname) {
    }

    public void setChannelIdEnabled(boolean enabled) {
    }

    public byte[] getNpnSelectedProtocol() {
        return null;
    }

    public void setNpnProtocols(byte[] npnProtocols) {
    }
}
