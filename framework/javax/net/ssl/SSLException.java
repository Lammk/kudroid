package javax.net.ssl;

import java.io.IOException;

public class SSLException extends IOException {
    private static final long serialVersionUID = 4511006460562611967L;
    public SSLException(String reason) { super(reason); }
    public SSLException(String message, Throwable cause) { super(message, cause); }
    public SSLException(Throwable cause) { super(cause); }
}
