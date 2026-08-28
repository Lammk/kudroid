package javax.net;

import java.io.IOException;
import java.net.InetAddress;
import java.net.Socket;
import java.net.UnknownHostException;

public abstract class SocketFactory {
    private static SocketFactory theFactory;

    protected SocketFactory() {}
    public static synchronized SocketFactory getDefault() {
        if (theFactory == null) theFactory = new DefaultSocketFactory();
        return theFactory;
    }
    public Socket createSocket() throws IOException { return new Socket(); }
    public abstract Socket createSocket(String host, int port) throws IOException, UnknownHostException;
    public abstract Socket createSocket(String host, int port, InetAddress localHost, int localPort) throws IOException, UnknownHostException;
    public abstract Socket createSocket(InetAddress host, int port) throws IOException;
    public abstract Socket createSocket(InetAddress address, int port, InetAddress localAddress, int localPort) throws IOException;

    private static class DefaultSocketFactory extends SocketFactory {
        public Socket createSocket(String host, int port) throws IOException { return new Socket(host, port); }
        public Socket createSocket(String host, int port, InetAddress localHost, int localPort) throws IOException { return new Socket(host, port, localHost, localPort); }
        public Socket createSocket(InetAddress host, int port) throws IOException { return new Socket(host, port); }
        public Socket createSocket(InetAddress address, int port, InetAddress localAddress, int localPort) throws IOException { return new Socket(address, port, localAddress, localPort); }
    }
}
