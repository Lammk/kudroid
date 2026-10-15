package java.net;

/** The platform's http: connection; all behavior lives in HttpURLConnection. */
final class KuHttpURLConnection extends HttpURLConnection {
    private final Proxy proxy;

    KuHttpURLConnection(URL url, Proxy proxy) {
        super(url);
        this.proxy = proxy;
    }

    @Override
    public void disconnect() {
        kuDisconnect();
    }

    @Override
    public boolean usingProxy() {
        return proxy != null && proxy.type() != Proxy.Type.DIRECT;
    }
}
