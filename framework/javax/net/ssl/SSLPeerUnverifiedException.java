package javax.net.ssl;

import javax.net.ssl.SSLException;

public class SSLPeerUnverifiedException extends SSLException {
    public SSLPeerUnverifiedException(String reason) { super(reason); }
}
