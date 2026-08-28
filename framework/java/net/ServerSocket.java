package java.net;

import java.io.IOException;
import java.io.Closeable;

public class ServerSocket implements Closeable {
    private boolean closed = false;
    private boolean bound = false;

    public ServerSocket() throws IOException {}
    public ServerSocket(int port) throws IOException { this(port, 50, null); }
    public ServerSocket(int port, int backlog) throws IOException { this(port, backlog, null); }
    public ServerSocket(int port, int backlog, InetAddress bindAddr) throws IOException {
        bind(new InetSocketAddress(bindAddr, port), backlog);
    }
    public void bind(SocketAddress endpoint) throws IOException { bind(endpoint, 50); }
    public void bind(SocketAddress endpoint, int backlog) throws IOException { bound = true; }
    public InetAddress getInetAddress() { return null; }
    public int getLocalPort() { return 0; }
    public SocketAddress getLocalSocketAddress() { return null; }
    public Socket accept() throws IOException {
        return new Socket();
    }
    public void close() throws IOException { closed = true; }
    public boolean isBound() { return bound; }
    public boolean isClosed() { return closed; }
    public synchronized void setSoTimeout(int timeout) throws SocketException {}
    public synchronized int getSoTimeout() throws IOException { return 0; }
    public void setReuseAddress(boolean on) throws SocketException {}
    public boolean getReuseAddress() throws SocketException { return false; }
}
