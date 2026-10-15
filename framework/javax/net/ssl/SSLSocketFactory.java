package javax.net.ssl;

import java.io.IOException;
import java.net.Socket;
import javax.net.SocketFactory;
import libcore.net.ssl.KuSSLSocketFactory;

public abstract class SSLSocketFactory extends SocketFactory {
    private static SSLSocketFactory defaultFactory;

    public SSLSocketFactory() {}

    public static synchronized SocketFactory getDefault() {
        if (defaultFactory == null) defaultFactory = new KuSSLSocketFactory(null);
        return defaultFactory;
    }

    public abstract String[] getDefaultCipherSuites();
    public abstract String[] getSupportedCipherSuites();
    public abstract Socket createSocket(Socket s, String host, int port, boolean autoClose) throws IOException;
}
