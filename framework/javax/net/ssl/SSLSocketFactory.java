package javax.net.ssl;

import java.io.IOException;
import java.net.Socket;
import java.net.UnknownHostException;
import java.net.InetAddress;
import javax.net.SocketFactory;

public abstract class SSLSocketFactory extends SocketFactory {
    private static SSLSocketFactory theFactory;

    public SSLSocketFactory() {}

    public static synchronized SocketFactory getDefault() {
        if (theFactory == null) {
            theFactory = new DefaultSSLSocketFactory();
        }
        return theFactory;
    }

    public abstract String[] getDefaultCipherSuites();
    public abstract String[] getSupportedCipherSuites();
    public abstract Socket createSocket(Socket s, String host, int port, boolean autoClose) throws IOException;
}
