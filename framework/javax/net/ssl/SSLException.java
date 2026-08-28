package javax.net.ssl;

import java.io.IOException;

public class SSLException extends IOException {
    public SSLException(String reason) { super(reason); }
    public SSLException(String message, Throwable cause) { super(message, cause); }
    public SSLException(Throwable cause) { super(cause); }
}
