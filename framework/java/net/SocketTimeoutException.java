package java.net;

import java.io.InterruptedIOException;

public class SocketTimeoutException extends InterruptedIOException {
    private static final long serialVersionUID = -8846590140380363298L;
    public SocketTimeoutException(String msg) { super(msg); }
    public SocketTimeoutException() { super(); }
}
