package java.net;

public final class DatagramPacket {
    private byte[] buf;
    private int offset;
    private int length;
    private InetAddress address;
    private int port;

    public DatagramPacket(byte[] buf, int offset, int length) {
        setData(buf, offset, length);
    }
    public DatagramPacket(byte[] buf, int length) {
        this(buf, 0, length);
    }
    public DatagramPacket(byte[] buf, int offset, int length, InetAddress address, int port) {
        this(buf, offset, length);
        setAddress(address);
        setPort(port);
    }
    public DatagramPacket(byte[] buf, int length, InetAddress address, int port) {
        this(buf, 0, length, address, port);
    }

    public synchronized InetAddress getAddress() { return address; }
    public synchronized int getPort() { return port; }
    public synchronized byte[] getData() { return buf; }
    public synchronized int getOffset() { return offset; }
    public synchronized int getLength() { return length; }
    public synchronized void setData(byte[] buf, int offset, int length) {
        this.buf = buf;
        this.offset = offset;
        this.length = length;
    }
    public synchronized void setAddress(InetAddress iaddr) { address = iaddr; }
    public synchronized void setPort(int iport) { port = iport; }
    public synchronized void setLength(int len) { length = len; }
}
