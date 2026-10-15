package java.net;

import java.io.IOException;

public abstract class URLStreamHandler {
    protected abstract URLConnection openConnection(URL u) throws IOException;
    protected URLConnection openConnection(URL u, Proxy p) throws IOException {
        return openConnection(u);
    }
}
