package java.net;

public class NoRouteToHostException extends SocketException {
    private static final long serialVersionUID = -1896960767916627766L;
    public NoRouteToHostException(String msg) { super(msg); }
    public NoRouteToHostException() { super(); }
}
