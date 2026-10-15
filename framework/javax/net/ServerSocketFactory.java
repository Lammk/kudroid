package javax.net;

import java.io.IOException;
import java.net.InetAddress;
import java.net.ServerSocket;

public abstract class ServerSocketFactory {
    private static ServerSocketFactory theFactory;

    protected ServerSocketFactory() {}
    public static synchronized ServerSocketFactory getDefault() {
        if (theFactory == null) theFactory = new DefaultServerSocketFactory();
        return theFactory;
    }
    public ServerSocket createServerSocket() throws IOException { return new ServerSocket(); }
    public abstract ServerSocket createServerSocket(int port) throws IOException;
    public abstract ServerSocket createServerSocket(int port, int backlog) throws IOException;
    public abstract ServerSocket createServerSocket(int port, int backlog, InetAddress ifAddress) throws IOException;

    private static class DefaultServerSocketFactory extends ServerSocketFactory {
        public ServerSocket createServerSocket(int port) throws IOException { return new ServerSocket(port); }
        public ServerSocket createServerSocket(int port, int backlog) throws IOException { return new ServerSocket(port, backlog); }
        public ServerSocket createServerSocket(int port, int backlog, InetAddress ifAddress) throws IOException { return new ServerSocket(port, backlog, ifAddress); }
    }
}
