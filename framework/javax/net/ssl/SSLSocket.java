package javax.net.ssl;

import java.io.IOException;
import java.net.Socket;

public abstract class SSLSocket extends Socket {
    protected SSLSocket() {}

    public abstract String[] getSupportedCipherSuites();
    public abstract String[] getEnabledCipherSuites();
    public abstract void setEnabledCipherSuites(String[] suites);
    public abstract String[] getSupportedProtocols();
    public abstract String[] getEnabledProtocols();
    public abstract void setEnabledProtocols(String[] protocols);
    public abstract SSLSession getSession();
    public abstract void startHandshake() throws IOException;

    public SSLSession getHandshakeSession() { return null; }
    public void addHandshakeCompletedListener(HandshakeCompletedListener listener) {}
    public void removeHandshakeCompletedListener(HandshakeCompletedListener listener) {}
    public void setUseClientMode(boolean mode) {
        if (!mode) throw new IllegalArgumentException("server mode is not supported");
    }
    public boolean getUseClientMode() { return true; }
    public void setNeedClientAuth(boolean need) {}
    public boolean getNeedClientAuth() { return false; }
    public void setWantClientAuth(boolean want) {}
    public boolean getWantClientAuth() { return false; }
    public void setEnableSessionCreation(boolean flag) {}
    public boolean getEnableSessionCreation() { return true; }

    public SSLParameters getSSLParameters() {
        SSLParameters p = new SSLParameters(getEnabledCipherSuites(), getEnabledProtocols());
        return p;
    }

    public void setSSLParameters(SSLParameters params) {
        if (params == null) return;
        if (params.getCipherSuites() != null) setEnabledCipherSuites(params.getCipherSuites());
        if (params.getProtocols() != null) setEnabledProtocols(params.getProtocols());
    }

    /** The protocol negotiated through ALPN, or null when none was. */
    public String getApplicationProtocol() { return null; }
    public String getHandshakeApplicationProtocol() { return getApplicationProtocol(); }

    /** The server name to present, for stacks that set it before connecting. */
    public void setHostname(String hostname) {}
}
