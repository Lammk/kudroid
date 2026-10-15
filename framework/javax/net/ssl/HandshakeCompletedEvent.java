package javax.net.ssl;

import java.util.EventObject;

public class HandshakeCompletedEvent extends EventObject {
    private static final long serialVersionUID = 7914963744257769778L;
    private final transient SSLSession session;

    public HandshakeCompletedEvent(SSLSocket sock, SSLSession s) {
        super(sock);
        this.session = s;
    }

    public SSLSession getSession() { return session; }
    public String getCipherSuite() { return session.getCipherSuite(); }
    public SSLSocket getSocket() { return (SSLSocket) getSource(); }
}
