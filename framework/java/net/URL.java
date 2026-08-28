package java.net;

import java.io.Serializable;
import java.io.InputStream;
import java.io.IOException;

public final class URL implements Serializable {
    private static final long serialVersionUID = -7627629688361524110L;
    private final String protocol;
    private final String host;
    private final int port;
    private final String file;
    private final String spec;

    public URL(String spec) throws MalformedURLException {
        this.spec = spec;
        this.protocol = spec.startsWith("https") ? "https" : "http";
        this.host = "localhost";
        this.port = spec.startsWith("https") ? 443 : 80;
        this.file = "/";
    }
    public URL(String protocol, String host, int port, String file) throws MalformedURLException {
        this.protocol = protocol;
        this.host = host;
        this.port = port;
        this.file = file;
        this.spec = protocol + "://" + host + ":" + port + file;
    }
    public URL(String protocol, String host, String file) throws MalformedURLException {
        this(protocol, host, -1, file);
    }
    public URL(URL context, String spec) throws MalformedURLException {
        this(spec);
    }

    public String getProtocol() { return protocol; }
    public String getHost() { return host; }
    public int getPort() { return port; }
    public int getDefaultPort() { return protocol.equals("https") ? 443 : 80; }
    public String getFile() { return file; }
    public String getPath() { return file; }
    public String getQuery() { return null; }
    public String getAuthority() { return host; }
    public URLConnection openConnection() throws IOException {
        return new HttpURLConnection(this) {
            public void disconnect() {}
            public boolean usingProxy() { return false; }
            public void connect() throws IOException {}
        };
    }
    public URLConnection openConnection(Proxy proxy) throws IOException {
        return openConnection();
    }
    public InputStream openStream() throws IOException {
        return openConnection().getInputStream();
    }
    public String toString() { return spec; }
    public String toExternalForm() { return spec; }
    public URI toURI() throws URISyntaxException { return new URI(spec); }
}
