package javax.net.ssl;

import java.net.Socket;
import java.io.IOException;

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
}
