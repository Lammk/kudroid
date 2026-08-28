package java.net;

import java.io.IOException;

public class MulticastSocket extends DatagramSocket {
    public MulticastSocket() throws IOException { super(); }
    public MulticastSocket(int port) throws IOException { super(port); }
    public MulticastSocket(SocketAddress bindaddr) throws IOException { super(bindaddr); }
    public void joinGroup(InetAddress mcastaddr) throws IOException {}
    public void leaveGroup(InetAddress mcastaddr) throws IOException {}
}
