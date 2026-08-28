package java.net;

import java.io.IOException;

public class SocketException extends IOException {
    private static final long serialVersionUID = -5938955648563334464L;
    public SocketException(String msg) { super(msg); }
    public SocketException() { super(); }
}
