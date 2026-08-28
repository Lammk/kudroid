package javax.net.ssl;

import javax.net.SocketFactory;
import java.io.IOException;
import java.net.Socket;
import java.net.UnknownHostException;

public abstract class SSLSocketFactory extends SocketFactory {
    private static SSLSocketFactory defaultFactory;

    public SSLSocketFactory() {}
    public static synchronized SocketFactory getDefault() {
        if (defaultFactory == null) defaultFactory = new DefaultSSLSocketFactory();
        return defaultFactory;
    }
    public abstract String[] getDefaultCipherSuites();
    public abstract String[] getSupportedCipherSuites();
    public abstract Socket createSocket(Socket s, String host, int port, boolean autoClose) throws IOException;

    private static class DefaultSSLSocketFactory extends SSLSocketFactory {
        public String[] getDefaultCipherSuites() { return new String[0]; }
        public String[] getSupportedCipherSuites() { return new String[0]; }
        public Socket createSocket(Socket s, String host, int port, boolean autoClose) throws IOException { return new Socket(host, port); }
        public Socket createSocket(String host, int port) throws IOException { return new Socket(host, port); }
        public Socket createSocket(String host, int port, java.net.InetAddress localHost, int localPort) throws IOException { return new Socket(host, port, localHost, localPort); }
        public Socket createSocket(java.net.InetAddress host, int port) throws IOException { return new Socket(host, port); }
        public Socket createSocket(java.net.InetAddress address, int port, java.net.InetAddress localAddress, int localPort) throws IOException { return new Socket(address, port, localAddress, localPort); }
    }
}
