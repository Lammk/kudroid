package java.net;

import java.io.IOException;
import java.util.List;
import java.util.Collections;

public abstract class ProxySelector {
    private static ProxySelector theProxySelector = new DefaultProxySelector();

    public ProxySelector() {}

    public static synchronized ProxySelector getDefault() {
        return theProxySelector;
    }

    public static synchronized void setDefault(ProxySelector csel) {
        theProxySelector = csel;
    }

    public abstract List<Proxy> select(URI uri);
    public abstract void connectFailed(URI uri, SocketAddress sa, IOException ioe);

    private static class DefaultProxySelector extends ProxySelector {
        public List<Proxy> select(URI uri) {
            return Collections.singletonList(Proxy.NO_PROXY);
        }
        public void connectFailed(URI uri, SocketAddress sa, IOException ioe) {}
    }
}
