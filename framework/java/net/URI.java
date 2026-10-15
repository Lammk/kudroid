package java.net;

import java.io.Serializable;

/** A parsed URI reference (RFC 2396 syntax, RFC 3986 resolution). */
public final class URI implements Comparable<URI>, Serializable {
    private static final long serialVersionUID = -6052424284110960213L;
    private static final char[] HEX = "0123456789ABCDEF".toCharArray();

    private String string;
    private String scheme;
    private String fragment;
    private String authority;
    private String userInfo;
    private String host;
    private int port = -1;
    private String path;
    private String query;
    private String schemeSpecificPart;

    private URI() {}

    public URI(String str) throws URISyntaxException {
        parse(str);
    }

    public URI(String scheme, String ssp, String fragment) throws URISyntaxException {
        this(buildString(scheme, ssp, null, null, null, -1, null, null, fragment));
    }

    public URI(String scheme, String userInfo, String host, int port, String path, String query,
            String fragment) throws URISyntaxException {
        this(buildString(scheme, null, null, userInfo, host, port, path, query, fragment));
    }

    public URI(String scheme, String authority, String path, String query, String fragment)
            throws URISyntaxException {
        this(buildString(scheme, null, authority, null, null, -1, path, query, fragment));
    }

    public URI(String scheme, String host, String path, String fragment) throws URISyntaxException {
        this(scheme, null, host, -1, path, null, fragment);
    }

    public static URI create(String str) {
        try {
            return new URI(str);
        } catch (URISyntaxException x) {
            throw new IllegalArgumentException(x.getMessage(), x);
        }
    }

    // ── parsing ──────────────────────────────────────────────────────────────

    private void parse(String input) throws URISyntaxException {
        if (input == null) throw new NullPointerException("str == null");
        string = input;
        int n = input.length();
        int p = 0;

        int hash = input.indexOf('#');
        String rest = input;
        if (hash >= 0) {
            fragment = input.substring(hash + 1);
            checkChars(fragment, "fragment", hash + 1, input, false);
            rest = input.substring(0, hash);
        }

        // scheme: letters, digits, '+', '-', '.' up to a ':' that precedes any '/', '?', '#'
        int colon = rest.indexOf(':');
        int firstSep = firstOf(rest, "/?");
        if (colon >= 0 && (firstSep < 0 || colon < firstSep)) {
            if (colon == 0) throw new URISyntaxException(input, "Expected scheme name", 0);
            for (int i = 0; i < colon; i++) {
                char c = rest.charAt(i);
                boolean letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
                boolean other = (c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.';
                if (!(letter || (i > 0 && other))) {
                    throw new URISyntaxException(input, "Illegal character in scheme name", i);
                }
            }
            scheme = rest.substring(0, colon);
            p = colon + 1;
        }

        String ssp = rest.substring(p);
        schemeSpecificPart = ssp;
        if (scheme != null && !ssp.startsWith("/")) {
            // Opaque: nothing past the scheme is interpreted.
            if (ssp.length() == 0) {
                throw new URISyntaxException(input, "Expected scheme-specific part", p);
            }
            checkChars(ssp, "opaque part", p, input, false);
            return;
        }
        checkChars(ssp, "hierarchical part", p, input, true);

        int q = 0;
        if (ssp.startsWith("//")) {
            int end = firstOf(ssp.substring(2), "/?");
            int auth = end < 0 ? ssp.length() : end + 2;
            authority = ssp.substring(2, auth);
            q = auth;
            if (authority.length() == 0) {
                // "file:///x": an empty authority is allowed only when a path follows.
                authority = null;
                if (q >= ssp.length()) {
                    throw new URISyntaxException(input, "Expected authority", p + 2);
                }
            } else {
                parseAuthority();
            }
        }
        int qm = ssp.indexOf('?', q);
        if (qm >= 0) {
            path = ssp.substring(q, qm);
            query = ssp.substring(qm + 1);
        } else {
            path = ssp.substring(q);
        }
    }

    private void parseAuthority() {
        // server-based: [userinfo@]host[:port]; anything else stays registry-based (host null).
        String a = authority;
        String ui = null;
        int at = a.lastIndexOf('@');
        String hp = a;
        if (at >= 0) {
            ui = a.substring(0, at);
            hp = a.substring(at + 1);
        }
        String h = hp;
        int prt = -1;
        if (hp.startsWith("[")) {
            int close = hp.indexOf(']');
            if (close < 0) return;
            h = hp.substring(0, close + 1);
            String after = hp.substring(close + 1);
            if (after.length() > 0) {
                if (after.charAt(0) != ':') return;
                prt = parsePort(after.substring(1));
                if (prt == -2) return;
            }
        } else {
            int c = hp.lastIndexOf(':');
            if (c >= 0) {
                h = hp.substring(0, c);
                prt = parsePort(hp.substring(c + 1));
                if (prt == -2) return;
            }
        }
        if (h.length() == 0) return;
        if (!h.startsWith("[")) {
            for (int i = 0; i < h.length(); i++) {
                char ch = h.charAt(i);
                boolean ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')
                        || (ch >= '0' && ch <= '9') || ch == '-' || ch == '.' || ch == '_';
                if (!ok) return;
            }
        }
        userInfo = ui;
        host = h;
        port = prt;
    }

    /** The port, -1 when empty, -2 when malformed. */
    private static int parsePort(String s) {
        if (s.length() == 0) return -1;
        int v = 0;
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            if (c < '0' || c > '9') return -2;
            v = v * 10 + (c - '0');
            if (v > 65535) return -2;
        }
        return v;
    }

    private static int firstOf(String s, String chars) {
        for (int i = 0; i < s.length(); i++) {
            if (chars.indexOf(s.charAt(i)) >= 0) return i;
        }
        return -1;
    }

    /** Rejects characters a URI cannot contain; '[' and ']' only where the authority allows them. */
    private static void checkChars(String s, String what, int offset, String input, boolean allowBrackets)
            throws URISyntaxException {
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            boolean bad = c <= ' ' || c == 0x7f || c == '"' || c == '<' || c == '>' || c == '\\'
                    || c == '^' || c == '`' || c == '{' || c == '|' || c == '}';
            if (!bad && (c == '[' || c == ']') && !allowBrackets) bad = true;
            if (!bad && c == '%') {
                if (i + 2 >= s.length() || Character.digit(s.charAt(i + 1), 16) < 0
                        || Character.digit(s.charAt(i + 2), 16) < 0) {
                    throw new URISyntaxException(input, "Malformed escape pair in " + what, offset + i);
                }
            }
            if (bad) {
                throw new URISyntaxException(input, "Illegal character in " + what, offset + i);
            }
        }
    }

    // ── building from components ─────────────────────────────────────────────

    private static String buildString(String scheme, String ssp, String authority, String userInfo,
            String host, int port, String path, String query, String fragment) {
        StringBuilder sb = new StringBuilder();
        if (scheme != null) sb.append(scheme).append(':');
        if (ssp != null) {
            sb.append(quote(ssp, "?/:@&=+$,;[]-_.!~*'()"));
        } else {
            if (host != null || authority != null) {
                sb.append("//");
                if (authority != null) {
                    sb.append(quote(authority, ":@&=+$,;[]-_.!~*'()"));
                } else {
                    if (userInfo != null) sb.append(quote(userInfo, ";:&=+$,-_.!~*'()")).append('@');
                    boolean v6 = host.indexOf(':') >= 0 && !host.startsWith("[");
                    sb.append(v6 ? "[" + host + "]" : host);
                    if (port != -1) sb.append(':').append(port);
                }
            }
            if (path != null) sb.append(quote(path, "/:@&=+$,;-_.!~*'()"));
            if (query != null) sb.append('?').append(quote(query, "?/:@&=+$,;[]-_.!~*'()"));
        }
        if (fragment != null) sb.append('#').append(quote(fragment, "?/:@&=+$,;[]-_.!~*'()"));
        return sb.toString();
    }

    /** Percent-encodes ASCII characters outside the unreserved set and `allowed`; '%' is always encoded. */
    private static String quote(String s, String allowed) {
        StringBuilder sb = null;
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            boolean ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
                    || allowed.indexOf(c) >= 0 || c >= 0x80;
            if (c == '%') ok = false;
            if (ok) {
                if (sb != null) sb.append(c);
                continue;
            }
            if (sb == null) {
                sb = new StringBuilder();
                sb.append(s.substring(0, i));
            }
            sb.append('%').append(HEX[(c >> 4) & 0xf]).append(HEX[c & 0xf]);
        }
        return sb == null ? s : sb.toString();
    }

    // ── accessors ────────────────────────────────────────────────────────────

    public String getScheme() { return scheme; }
    public boolean isAbsolute() { return scheme != null; }
    public boolean isOpaque() { return path == null; }
    public String getRawSchemeSpecificPart() { return schemeSpecificPart; }
    public String getSchemeSpecificPart() { return decode(schemeSpecificPart); }
    public String getRawAuthority() { return authority; }
    public String getAuthority() { return decode(authority); }
    public String getRawUserInfo() { return userInfo; }
    public String getUserInfo() { return decode(userInfo); }
    public String getHost() { return host; }
    public int getPort() { return port; }
    public String getRawPath() { return path; }
    public String getPath() { return decode(path); }
    public String getRawQuery() { return query; }
    public String getQuery() { return decode(query); }
    public String getRawFragment() { return fragment; }
    public String getFragment() { return decode(fragment); }

    public URI parseServerAuthority() throws URISyntaxException {
        if (host == null && authority != null) {
            throw new URISyntaxException(string, "Illegal character in authority", 0);
        }
        return this;
    }

    private static String decode(String s) {
        if (s == null || s.indexOf('%') < 0) return s;
        StringBuilder out = new StringBuilder(s.length());
        int n = s.length();
        int i = 0;
        while (i < n) {
            char c = s.charAt(i);
            if (c != '%') {
                out.append(c);
                i++;
                continue;
            }
            byte[] buf = new byte[(n - i) / 3];
            int len = 0;
            while (i + 2 < n && s.charAt(i) == '%' && len < buf.length) {
                buf[len++] = (byte) ((Character.digit(s.charAt(i + 1), 16) << 4)
                        | Character.digit(s.charAt(i + 2), 16));
                i += 3;
            }
            out.append(new String(buf, 0, len));
        }
        return out.toString();
    }

    // ── resolution ───────────────────────────────────────────────────────────

    public URI resolve(String str) {
        return resolve(create(str));
    }

    public URI resolve(URI child) {
        if (child.isOpaque() || isOpaque()) return child;
        if (child.scheme != null) return child;

        URI r = new URI();
        r.scheme = scheme;
        if (child.authority != null) {
            r.authority = child.authority;
            r.userInfo = child.userInfo;
            r.host = child.host;
            r.port = child.port;
            r.path = removeDots(child.path);
            r.query = child.query;
        } else {
            r.authority = authority;
            r.userInfo = userInfo;
            r.host = host;
            r.port = port;
            if (child.path.length() == 0) {
                r.path = path;
                r.query = child.query != null ? child.query : query;
            } else {
                if (child.path.startsWith("/")) {
                    r.path = removeDots(child.path);
                } else {
                    int slash = path.lastIndexOf('/');
                    String merged = (authority != null && path.length() == 0)
                            ? "/" + child.path
                            : path.substring(0, slash + 1) + child.path;
                    r.path = removeDots(merged);
                }
                r.query = child.query;
            }
        }
        r.fragment = child.fragment;
        r.rebuild();
        return r;
    }

    private void rebuild() {
        StringBuilder sb = new StringBuilder();
        if (scheme != null) sb.append(scheme).append(':');
        StringBuilder ssp = new StringBuilder();
        if (authority != null) ssp.append("//").append(authority);
        else if (host != null) ssp.append("//").append(host);
        ssp.append(path);
        if (query != null) ssp.append('?').append(query);
        schemeSpecificPart = ssp.toString();
        sb.append(schemeSpecificPart);
        if (fragment != null) sb.append('#').append(fragment);
        string = sb.toString();
    }

    /** Removes "." and ".." segments (RFC 3986 section 5.2.4). */
    private static String removeDots(String path) {
        if (path == null || path.indexOf('.') < 0) return path;
        StringBuilder out = new StringBuilder();
        String in = path;
        while (in.length() > 0) {
            if (in.startsWith("../")) {
                in = in.substring(3);
            } else if (in.startsWith("./")) {
                in = in.substring(2);
            } else if (in.startsWith("/./")) {
                in = in.substring(2);
            } else if (in.equals("/.")) {
                in = "/";
            } else if (in.startsWith("/../")) {
                in = in.substring(3);
                trimLastSegment(out);
            } else if (in.equals("/..")) {
                in = "/";
                trimLastSegment(out);
            } else if (in.equals(".") || in.equals("..")) {
                in = "";
            } else {
                int start = in.startsWith("/") ? 1 : 0;
                int next = in.indexOf('/', start);
                if (next < 0) next = in.length();
                out.append(in.substring(0, next));
                in = in.substring(next);
            }
        }
        return out.toString();
    }

    private static void trimLastSegment(StringBuilder out) {
        int slash = out.toString().lastIndexOf('/');
        out.setLength(slash < 0 ? 0 : slash);
    }

    public URI normalize() {
        if (isOpaque() || path.length() == 0) return this;
        String np = removeDots(path);
        if (np.equals(path)) return this;
        URI r = new URI();
        r.scheme = scheme;
        r.authority = authority;
        r.userInfo = userInfo;
        r.host = host;
        r.port = port;
        r.path = np;
        r.query = query;
        r.fragment = fragment;
        r.rebuild();
        return r;
    }

    public URI relativize(URI uri) {
        if (uri.isOpaque() || isOpaque()) return uri;
        if (!equalIgnoringCase(scheme, uri.scheme) || !eq(authority, uri.authority)) return uri;
        String bp = normalize().path;
        String cp = uri.normalize().path;
        if (!bp.equals(cp)) {
            if (!bp.endsWith("/")) bp = bp + "/";
            if (!cp.startsWith(bp)) return uri;
        }
        URI r = new URI();
        r.path = cp.substring(bp.length());
        r.query = uri.query;
        r.fragment = uri.fragment;
        StringBuilder sb = new StringBuilder(r.path);
        if (r.query != null) sb.append('?').append(r.query);
        r.schemeSpecificPart = sb.toString();
        if (r.fragment != null) sb.append('#').append(r.fragment);
        r.string = sb.toString();
        return r;
    }

    // ── conversions ──────────────────────────────────────────────────────────

    public URL toURL() throws MalformedURLException {
        if (!isAbsolute()) throw new IllegalArgumentException("URI is not absolute");
        return new URL(toString());
    }

    @Override
    public String toString() { return string; }

    public String toASCIIString() {
        StringBuilder sb = null;
        for (int i = 0; i < string.length(); i++) {
            char c = string.charAt(i);
            if (c < 0x80) {
                if (sb != null) sb.append(c);
                continue;
            }
            if (sb == null) {
                sb = new StringBuilder();
                sb.append(string.substring(0, i));
            }
            int end = i + 1;
            while (end < string.length() && string.charAt(end) >= 0x80) end++;
            byte[] bytes = string.substring(i, end).getBytes();
            for (int b = 0; b < bytes.length; b++) {
                sb.append('%').append(HEX[(bytes[b] >> 4) & 0xf]).append(HEX[bytes[b] & 0xf]);
            }
            i = end - 1;
        }
        return sb == null ? string : sb.toString();
    }

    // ── identity ─────────────────────────────────────────────────────────────

    private static boolean eq(String a, String b) {
        return a == null ? b == null : a.equals(b);
    }

    private static boolean equalIgnoringCase(String a, String b) {
        return a == null ? b == null : a.equalsIgnoreCase(b);
    }

    @Override
    public boolean equals(Object ob) {
        if (ob == this) return true;
        if (!(ob instanceof URI)) return false;
        URI that = (URI) ob;
        if (isOpaque() != that.isOpaque()) return false;
        if (!equalIgnoringCase(scheme, that.scheme)) return false;
        if (!eq(fragment, that.fragment)) return false;
        if (isOpaque()) return eq(schemeSpecificPart, that.schemeSpecificPart);
        if (!eq(path, that.path) || !eq(query, that.query)) return false;
        if (authority == that.authority) return true;
        if (host != null) {
            return eq(userInfo, that.userInfo) && equalIgnoringCase(host, that.host)
                    && port == that.port;
        }
        return eq(authority, that.authority);
    }

    @Override
    public int hashCode() {
        int h = scheme == null ? 0 : scheme.toLowerCase().hashCode();
        h = h * 31 + (fragment == null ? 0 : fragment.hashCode());
        if (isOpaque()) return h * 31 + schemeSpecificPart.hashCode();
        h = h * 31 + (path == null ? 0 : path.hashCode());
        h = h * 31 + (query == null ? 0 : query.hashCode());
        if (host != null) {
            h = h * 31 + (userInfo == null ? 0 : userInfo.hashCode());
            h = h * 31 + host.toLowerCase().hashCode();
            h = h * 31 + port;
        } else if (authority != null) {
            h = h * 31 + authority.hashCode();
        }
        return h;
    }

    @Override
    public int compareTo(URI that) {
        return string.compareTo(that.string);
    }
}
