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
        int colon = spec.indexOf(':');
        if (colon > 0) {
            String proto = spec.substring(0, colon).toLowerCase();
            if (proto.equals("http") || proto.equals("https") || proto.equals("file")
                    || proto.equals("jar")) {
                this.protocol = proto;
                String rest = spec.substring(colon + 1);
                if (proto.equals("file") || proto.equals("jar")) {
                    this.host = "";
                    this.port = -1;
                    this.file = rest;
                } else {
                    this.host = "localhost";
                    this.port = proto.equals("https") ? 443 : 80;
                    this.file = "/";
                }
                return;
            }
        }
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
        if (spec.indexOf(':') > 0) {
            this.spec = spec;
            URL parsed = new URL(spec);
            this.protocol = parsed.protocol;
            this.host = parsed.host;
            this.port = parsed.port;
            this.file = parsed.file;
        } else if (context != null) {
            String base = context.file;
            int slash = base.lastIndexOf('/');
            String dir = slash >= 0 ? base.substring(0, slash + 1) : "/";
            this.protocol = context.protocol;
            this.host = context.host;
            this.port = context.port;
            this.file = dir + spec;
            this.spec = this.protocol + ":" + this.file;
        } else {
            throw new MalformedURLException("no context for relative URL " + spec);
        }
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
        if (protocol.equals("file")) {
            return new FileURLConnection(this);
        }
        if (protocol.equals("jar")) {
            return new JarURLConnection(this);
        }
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
