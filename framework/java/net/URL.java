package java.net;

import java.io.IOException;
import java.io.InputStream;
import java.io.Serializable;

/** A parsed URL; connections are opened by the platform's built-in handlers. */
public final class URL implements Serializable {
    private static final long serialVersionUID = -7627629688361524110L;
    private static URLStreamHandlerFactory sFactory;

    private String protocol;
    private String host;
    private int port = -1;
    private String path;
    private String query;
    private String ref;
    private String authority;
    private String userInfo;
    private transient URLStreamHandler handler;

    public URL(String spec) throws MalformedURLException {
        this(null, spec);
    }

    public URL(URL context, String spec) throws MalformedURLException {
        if (spec == null) throw new MalformedURLException("spec == null");
        parse(context, spec.trim());
    }

    public URL(URL context, String spec, URLStreamHandler handler) throws MalformedURLException {
        this(context, spec);
        this.handler = handler;
    }

    public URL(String protocol, String host, int port, String file) throws MalformedURLException {
        this(protocol, host, port, file, null);
    }

    public URL(String protocol, String host, String file) throws MalformedURLException {
        this(protocol, host, -1, file, null);
    }

    public URL(String protocol, String host, int port, String file, URLStreamHandler handler)
            throws MalformedURLException {
        if (protocol == null) throw new MalformedURLException("unknown protocol: null");
        this.protocol = protocol.toLowerCase();
        checkProtocol(this.protocol);
        if (port < -1) throw new MalformedURLException("Invalid port number :" + port);
        if (host != null) {
            if (host.indexOf(':') >= 0 && !host.startsWith("[")) host = "[" + host + "]";
            this.host = host;
            this.port = port;
            this.authority = port == -1 ? host : host + ":" + port;
        }
        String f = file == null ? "" : file;
        int hash = f.indexOf('#');
        if (hash >= 0) {
            ref = f.substring(hash + 1);
            f = f.substring(0, hash);
        }
        int q = f.indexOf('?');
        if (q >= 0) {
            query = f.substring(q + 1);
            f = f.substring(0, q);
        }
        this.path = f;
        this.handler = handler;
    }

    // ── parsing ──────────────────────────────────────────────────────────────

    private static void checkProtocol(String p) throws MalformedURLException {
        if (p.equals("http") || p.equals("https") || p.equals("file") || p.equals("jar")
                || p.equals("ftp")) {
            return;
        }
        if (sFactory != null && sFactory.createURLStreamHandler(p) != null) return;
        throw new MalformedURLException("unknown protocol: " + p);
    }

    private void parse(URL context, String spec) throws MalformedURLException {
        int hash = spec.indexOf('#');
        String frag = null;
        String body = spec;
        if (hash >= 0) {
            frag = spec.substring(hash + 1);
            body = spec.substring(0, hash);
        }

        int start = 0;
        String proto = null;
        int colon = body.indexOf(':');
        if (colon > 0 && isScheme(body, colon)) {
            proto = body.substring(0, colon).toLowerCase();
            start = colon + 1;
        }
        boolean inherit = false;
        if (proto == null) {
            if (context == null) throw new MalformedURLException("no protocol: " + spec);
            proto = context.protocol;
            inherit = true;
        } else if (context != null && proto.equals(context.protocol)
                && !body.startsWith("//", start) && !body.startsWith("/", start)) {
            // "http:relative" resolves against the context, as the platform does.
            inherit = true;
        }
        checkProtocol(proto);
        this.protocol = proto;
        this.handler = context != null && inherit ? context.handler : null;

        String rest = body.substring(start);
        if (rest.startsWith("//")) {
            int end = rest.length();
            for (int i = 2; i < rest.length(); i++) {
                char c = rest.charAt(i);
                if (c == '/' || c == '?') {
                    end = i;
                    break;
                }
            }
            setAuthority(rest.substring(2, end));
            rest = rest.substring(end);
            splitPathQuery(rest);
        } else if (inherit && context != null) {
            // Relative to the context.
            this.authority = context.authority;
            this.userInfo = context.userInfo;
            this.host = context.host;
            this.port = context.port;
            if (rest.length() == 0) {
                this.path = context.path;
                this.query = context.query;
                this.ref = hash >= 0 ? frag : context.ref;
                return;
            }
            if (rest.startsWith("?")) {
                this.path = context.path;
                this.query = rest.substring(1);
            } else if (rest.startsWith("/")) {
                splitPathQuery(rest);
            } else {
                String cp = context.path == null ? "" : context.path;
                int slash = cp.lastIndexOf('/');
                String merged = (cp.length() == 0 && context.authority != null ? "/" : "")
                        + cp.substring(0, slash + 1) + rest;
                splitPathQuery(merged);
            }
            this.path = normalizePath(this.path);
        } else {
            // No authority: opaque-style (file:/x, jar:..., mailto-like).
            splitPathQuery(rest);
        }
        this.ref = frag;
    }

    private static boolean isScheme(String s, int colon) {
        for (int i = 0; i < colon; i++) {
            char c = s.charAt(i);
            boolean letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
            boolean other = (c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.';
            if (!(letter || (i > 0 && other))) return false;
        }
        return true;
    }

    private void splitPathQuery(String s) {
        int q = s.indexOf('?');
        if (q >= 0) {
            this.path = s.substring(0, q);
            this.query = s.substring(q + 1);
        } else {
            this.path = s;
            this.query = null;
        }
    }

    private void setAuthority(String auth) throws MalformedURLException {
        if (auth.length() == 0) {
            this.authority = null;
            this.host = "";
            this.port = -1;
            return;
        }
        this.authority = auth;
        String hp = auth;
        int at = auth.lastIndexOf('@');
        if (at >= 0) {
            this.userInfo = auth.substring(0, at);
            hp = auth.substring(at + 1);
        }
        int portStart = -1;
        if (hp.startsWith("[")) {
            int close = hp.indexOf(']');
            if (close < 0) throw new MalformedURLException("Invalid host: " + hp);
            this.host = hp.substring(0, close + 1);
            if (close + 1 < hp.length()) {
                if (hp.charAt(close + 1) != ':') throw new MalformedURLException("Invalid authority: " + auth);
                portStart = close + 2;
            }
        } else {
            int c = hp.lastIndexOf(':');
            if (c >= 0) {
                this.host = hp.substring(0, c);
                portStart = c + 1;
            } else {
                this.host = hp;
            }
        }
        this.port = -1;
        if (portStart >= 0 && portStart < hp.length()) {
            int v = 0;
            for (int i = portStart; i < hp.length(); i++) {
                char ch = hp.charAt(i);
                if (ch < '0' || ch > '9') throw new MalformedURLException("Invalid port number :" + hp.substring(portStart));
                v = v * 10 + (ch - '0');
                if (v > 65535) throw new MalformedURLException("Invalid port number :" + v);
            }
            this.port = v;
        }
    }

    /** Collapses "/./" and "/../" in a path. */
    private static String normalizePath(String p) {
        if (p == null || (p.indexOf("/.") < 0)) return p;
        String[] parts = new String[16];
        int n = 0;
        boolean trailing = false;
        int i = 0;
        boolean absolute = p.startsWith("/");
        if (absolute) i = 1;
        while (i <= p.length()) {
            int next = p.indexOf('/', i);
            if (next < 0) next = p.length();
            String seg = p.substring(i, next);
            boolean last = next >= p.length();
            if (seg.equals(".")) {
                trailing = last;
            } else if (seg.equals("..")) {
                if (n > 0) n--;
                trailing = last;
            } else {
                if (n == parts.length) {
                    String[] bigger = new String[n * 2];
                    System.arraycopy(parts, 0, bigger, 0, n);
                    parts = bigger;
                }
                if (!(last && seg.length() == 0 && n > 0)) parts[n++] = seg;
                trailing = false;
            }
            if (last) break;
            i = next + 1;
        }
        StringBuilder sb = new StringBuilder();
        if (absolute) sb.append('/');
        for (int k = 0; k < n; k++) {
            if (k > 0) sb.append('/');
            sb.append(parts[k]);
        }
        if (trailing && n > 0) sb.append('/');
        return sb.toString();
    }

    // ── accessors ────────────────────────────────────────────────────────────

    public String getProtocol() { return protocol; }
    public String getHost() { return host == null ? "" : host; }
    public int getPort() { return port; }

    public int getDefaultPort() {
        if (protocol.equals("http")) return 80;
        if (protocol.equals("https")) return 443;
        if (protocol.equals("ftp")) return 21;
        return -1;
    }

    public String getPath() { return path == null ? "" : path; }
    public String getQuery() { return query; }
    public String getRef() { return ref; }
    public String getAuthority() { return authority; }
    public String getUserInfo() { return userInfo; }

    public String getFile() {
        String p = getPath();
        return query == null ? p : p + "?" + query;
    }

    public String toExternalForm() {
        StringBuilder sb = new StringBuilder();
        sb.append(protocol).append(':');
        if (authority != null && authority.length() > 0) sb.append("//").append(authority);
        sb.append(getPath());
        if (query != null) sb.append('?').append(query);
        if (ref != null) sb.append('#').append(ref);
        return sb.toString();
    }

    @Override
    public String toString() { return toExternalForm(); }

    public URI toURI() throws URISyntaxException { return new URI(toExternalForm()); }

    public boolean sameFile(URL other) {
        if (other == null) return false;
        return protocol.equals(other.protocol)
                && getHost().equalsIgnoreCase(other.getHost())
                && (port == -1 ? getDefaultPort() : port)
                        == (other.port == -1 ? other.getDefaultPort() : other.port)
                && getFile().equals(other.getFile());
    }

    @Override
    public boolean equals(Object obj) {
        if (!(obj instanceof URL)) return false;
        URL o = (URL) obj;
        if (!sameFile(o)) return false;
        return ref == null ? o.ref == null : ref.equals(o.ref);
    }

    @Override
    public int hashCode() {
        int h = protocol.hashCode();
        h = h * 31 + getHost().toLowerCase().hashCode();
        h = h * 31 + (port == -1 ? getDefaultPort() : port);
        h = h * 31 + getFile().hashCode();
        return h * 31 + (ref == null ? 0 : ref.hashCode());
    }

    // ── connecting ───────────────────────────────────────────────────────────

    public static synchronized void setURLStreamHandlerFactory(URLStreamHandlerFactory fac) {
        if (sFactory != null) throw new Error("factory already defined");
        sFactory = fac;
    }

    public URLConnection openConnection() throws IOException {
        URLStreamHandler h = handler;
        if (h == null && sFactory != null) h = sFactory.createURLStreamHandler(protocol);
        if (h != null) return h.openConnection(this);
        return builtInConnection(null);
    }

    public URLConnection openConnection(Proxy proxy) throws IOException {
        if (proxy == null) throw new IllegalArgumentException("proxy == null");
        URLStreamHandler h = handler;
        if (h == null && sFactory != null) h = sFactory.createURLStreamHandler(protocol);
        if (h != null) return h.openConnection(this, proxy);
        return builtInConnection(proxy);
    }

    private URLConnection builtInConnection(Proxy proxy) throws IOException {
        if (protocol.equals("file")) return new FileURLConnection(this);
        if (protocol.equals("jar")) return new JarURLConnection(this);
        if (protocol.equals("http")) return new KuHttpURLConnection(this, proxy);
        if (protocol.equals("https")) return new javax.net.ssl.KuHttpsURLConnection(this, proxy);
        throw new IOException("Unsupported protocol: " + protocol);
    }

    public InputStream openStream() throws IOException {
        return openConnection().getInputStream();
    }

    public Object getContent() throws IOException {
        return openConnection().getContent();
    }
}
