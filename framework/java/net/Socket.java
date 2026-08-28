package java.net;

import java.io.InputStream;
import java.io.OutputStream;
import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.Closeable;

public class Socket implements Closeable {
    private boolean connected = false;
    private boolean closed = false;
    private SocketAddress address;
    private InputStream in = new ByteArrayInputStream(new byte[0]);
    private OutputStream out = new ByteArrayOutputStream();

    public Socket() {}
    public Socket(Proxy proxy) {}
    public Socket(String host, int port) throws UnknownHostException, IOException {
        connect(new InetSocketAddress(host, port));
    }
    public Socket(InetAddress address, int port) throws IOException {
        connect(new InetSocketAddress(address, port));
    }
    public Socket(String host, int port, InetAddress localAddr, int localPort) throws IOException {
        connect(new InetSocketAddress(host, port));
    }
    public Socket(InetAddress address, int port, InetAddress localAddr, int localPort) throws IOException {
        connect(new InetSocketAddress(address, port));
    }

    public void connect(SocketAddress endpoint) throws IOException {
        connect(endpoint, 0);
    }
    public void connect(SocketAddress endpoint, int timeout) throws IOException {
        this.address = endpoint;
        this.connected = true;
    }
    public void bind(SocketAddress bindpoint) throws IOException {}
    public InetAddress getInetAddress() { return (address instanceof InetSocketAddress) ? ((InetSocketAddress) address).getAddress() : null; }
    public InetAddress getLocalAddress() { return null; }
    public int getPort() { return (address instanceof InetSocketAddress) ? ((InetSocketAddress) address).getPort() : 0; }
    public int getLocalPort() { return 0; }
    public SocketAddress getRemoteSocketAddress() { return address; }
    public SocketAddress getLocalSocketAddress() { return null; }
    public InputStream getInputStream() throws IOException { return in; }
    public OutputStream getOutputStream() throws IOException { return out; }
    public void setTcpNoDelay(boolean on) throws SocketException {}
    public boolean getTcpNoDelay() throws SocketException { return true; }
    public void setSoLinger(boolean on, int linger) throws SocketException {}
    public int getSoLinger() throws SocketException { return -1; }
    public void sendUrgentData(int data) throws IOException {}
    public void setOOBInline(boolean on) throws SocketException {}
    public boolean getOOBInline() throws SocketException { return false; }
    public synchronized void setSoTimeout(int timeout) throws SocketException {}
    public synchronized int getSoTimeout() throws SocketException { return 0; }
    public synchronized void setSendBufferSize(int size) throws SocketException {}
    public synchronized int getSendBufferSize() throws SocketException { return 65536; }
    public synchronized void setReceiveBufferSize(int size) throws SocketException {}
    public synchronized int getReceiveBufferSize() throws SocketException { return 65536; }
    public void setKeepAlive(boolean on) throws SocketException {}
    public boolean getKeepAlive() throws SocketException { return false; }
    public void setTrafficClass(int tc) throws SocketException {}
    public int getTrafficClass() throws SocketException { return 0; }
    public void setReuseAddress(boolean on) throws SocketException {}
    public boolean getReuseAddress() throws SocketException { return false; }
    public synchronized void close() throws IOException { closed = true; connected = false; }
    public void shutdownInput() throws IOException {}
    public void shutdownOutput() throws IOException {}
    public boolean isConnected() { return connected; }
    public boolean isBound() { return true; }
    public boolean isClosed() { return closed; }
    public boolean isInputShutdown() { return false; }
    public boolean isOutputShutdown() { return false; }
}
