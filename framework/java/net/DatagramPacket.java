package java.net;

public final class DatagramPacket {
    private byte[] buf;
    private int offset;
    private int length;
    private int bufLength;
    private InetAddress address;
    private int port = -1;

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
    public DatagramPacket(byte[] buf, int offset, int length, SocketAddress address) {
        this(buf, offset, length);
        setSocketAddress(address);
    }
    public DatagramPacket(byte[] buf, int length, SocketAddress address) {
        this(buf, 0, length, address);
    }

    public synchronized InetAddress getAddress() { return address; }
    public synchronized int getPort() { return port; }
    public synchronized byte[] getData() { return buf; }
    public synchronized int getOffset() { return offset; }
    public synchronized int getLength() { return length; }

    /** The capacity a receive may fill, however short earlier datagrams were. */
    synchronized int bufLength() { return bufLength; }

    public synchronized void setData(byte[] buf, int offset, int length) {
        if (length < 0 || offset < 0 || buf == null || length + offset > buf.length) {
            throw new IllegalArgumentException("illegal length or offset");
        }
        this.buf = buf;
        this.offset = offset;
        this.length = length;
        this.bufLength = length;
    }
    public synchronized void setData(byte[] buf) {
        if (buf == null) throw new NullPointerException("buf is null");
        setData(buf, 0, buf.length);
    }
    public synchronized void setAddress(InetAddress iaddr) { address = iaddr; }
    public synchronized void setPort(int iport) {
        if (iport < 0 || iport > 0xFFFF) throw new IllegalArgumentException("Port out of range:" + iport);
        port = iport;
    }
    public synchronized void setLength(int len) {
        if (len < 0 || len + offset > buf.length) throw new IllegalArgumentException("illegal length");
        length = len;
        bufLength = len;
    }
    public synchronized void setSocketAddress(SocketAddress address) {
        if (!(address instanceof InetSocketAddress)) {
            throw new IllegalArgumentException("unsupported address type");
        }
        InetSocketAddress isa = (InetSocketAddress) address;
        if (isa.isUnresolved()) throw new IllegalArgumentException("unresolved address");
        setAddress(isa.getAddress());
        setPort(isa.getPort());
    }
    public synchronized SocketAddress getSocketAddress() {
        return new InetSocketAddress(address, port);
    }

    /** Records a received datagram without shrinking the packet's capacity. */
    synchronized void received(int len, InetAddress from, int fromPort) {
        this.length = len;
        this.address = from;
        this.port = fromPort;
    }
}
