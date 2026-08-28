package javax.net.ssl;

import java.io.IOException;
import java.net.InetAddress;
import java.net.Socket;
import java.net.UnknownHostException;

public abstract class SSLSocket extends Socket {
    protected SSLSocket() { super(); }
    protected SSLSocket(String host, int port) throws IOException, UnknownHostException { super(host, port); }
    protected SSLSocket(InetAddress address, int port) throws IOException { super(address, port); }
    protected SSLSocket(String host, int port, InetAddress clientAddress, int clientPort) throws IOException, UnknownHostException { super(host, port, clientAddress, clientPort); }
    protected SSLSocket(InetAddress address, int port, InetAddress clientAddress, int clientPort) throws IOException { super(address, port, clientAddress, clientPort); }

    public abstract String[] getSupportedCipherSuites();
    public abstract String[] getEnabledCipherSuites();
    public abstract void setEnabledCipherSuites(String[] suites);
    public abstract String[] getSupportedProtocols();
    public abstract String[] getEnabledProtocols();
    public abstract void setEnabledProtocols(String[] protocols);
    public abstract SSLSession getSession();
    public abstract void startHandshake() throws IOException;
    public abstract void setUseClientMode(boolean mode);
    public abstract boolean getUseClientMode();
    public abstract void setNeedClientAuth(boolean need);
    public abstract boolean getNeedClientAuth();
    public abstract void setWantClientAuth(boolean want);
    public abstract boolean getWantClientAuth();
    public abstract void setEnableSessionCreation(boolean flag);
    public abstract boolean getEnableSessionCreation();
    public SSLParameters getSSLParameters() { return new SSLParameters(); }
    public void setSSLParameters(SSLParameters params) {}
}
