package java.net;

import java.io.IOException;
import java.io.Closeable;

public class DatagramSocket implements Closeable {
    public DatagramSocket() throws SocketException {}
    public DatagramSocket(SocketAddress bindaddr) throws SocketException {}
    public DatagramSocket(int port) throws SocketException { this(port, null); }
    public DatagramSocket(int port, InetAddress laddr) throws SocketException {}

    public void bind(SocketAddress addr) throws SocketException {}
    public void connect(SocketAddress addr) throws SocketException {}
    public void connect(InetAddress address, int port) {}
    public void disconnect() {}
    public boolean isConnected() { return false; }
    public boolean isBound() { return true; }
    public void send(DatagramPacket p) throws IOException {}
    public synchronized void receive(DatagramPacket p) throws IOException {}
    public void close() {}
    public boolean isClosed() { return false; }
    public synchronized void setSoTimeout(int timeout) throws SocketException {}
    public synchronized int getSoTimeout() throws SocketException { return 0; }
}
