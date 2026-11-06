package android.net.ssl;

import javax.net.ssl.SSLSocket;
import libcore.net.ssl.KuSSLSocket;

/** Additional options for sockets from the platform TLS provider. */
public final class SSLSockets {
    private SSLSockets() {}

    public static boolean isSupportedSocket(SSLSocket socket) {
        return socket instanceof KuSSLSocket;
    }

    public static void setUseSessionTickets(SSLSocket socket, boolean useSessionTickets) {
        if (!isSupportedSocket(socket)) {
            throw new IllegalArgumentException("Socket is not a supported socket.");
        }
        ((KuSSLSocket) socket).setUseSessionTickets(useSessionTickets);
    }
}
