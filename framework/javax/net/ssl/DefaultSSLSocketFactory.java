package javax.net.ssl;

import java.io.IOException;
import java.net.InetAddress;
import java.net.Socket;
import java.net.UnknownHostException;

class DefaultSSLSocketFactory extends SSLSocketFactory {
    public String[] getDefaultCipherSuites() { return new String[0]; }
    public String[] getSupportedCipherSuites() { return new String[0]; }
    public Socket createSocket(Socket s, String host, int port, boolean autoClose) throws IOException {
        return new Socket(host, port);
    }
    public Socket createSocket(String host, int port) throws IOException, UnknownHostException {
        return new Socket(host, port);
    }
    public Socket createSocket(String host, int port, InetAddress localHost, int localPort) throws IOException, UnknownHostException {
        return new Socket(host, port, localHost, localPort);
    }
    public Socket createSocket(InetAddress host, int port) throws IOException {
        return new Socket(host, port);
    }
    public Socket createSocket(InetAddress address, int port, InetAddress localAddress, int localPort) throws IOException {
        return new Socket(address, port, localAddress, localPort);
    }
}
