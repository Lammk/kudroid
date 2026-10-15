package java.net;

public class PortUnreachableException extends SocketException {
    private static final long serialVersionUID = 8462548682014330093L;
    public PortUnreachableException(String msg) { super(msg); }
    public PortUnreachableException() { super(); }
}
