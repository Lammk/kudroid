package java.net;

import java.util.ArrayList;
import java.util.List;
import libcore.net.http.HttpDate;

/** An HTTP cookie as sent in Set-Cookie. */
public final class HttpCookie implements Cloneable {
    private final String name;
    private String value;
    private String comment;
    private String domain;
    private String path;
    private String portlist;
    private long maxAge = -1;
    private boolean secure;
    private boolean httpOnly;
    private int version = 1;
    private final long whenCreated = System.currentTimeMillis();

    /** True when the server named no Domain, so only the origin host may receive it. */
    boolean hostOnly;

    public HttpCookie(String name, String value) {
        if (name == null) throw new NullPointerException("name == null");
        String n = name.trim();
        if (n.length() == 0 || n.indexOf(';') >= 0 || n.indexOf(',') >= 0 || n.indexOf(' ') >= 0
                || n.charAt(0) == '$') {
            throw new IllegalArgumentException("Illegal cookie name");
        }
        this.name = n;
        this.value = value;
    }

    public static List<HttpCookie> parse(String header) {
        if (header == null) throw new NullPointerException("header == null");
        String h = header;
        boolean set2 = false;
        if (h.toLowerCase().startsWith("set-cookie2:")) {
            h = h.substring(12);
            set2 = true;
        } else if (h.toLowerCase().startsWith("set-cookie:")) {
            h = h.substring(11);
        }
        ArrayList<HttpCookie> out = new ArrayList<HttpCookie>();
        // One header can carry several cookies separated by commas only in the legacy
        // "Set-Cookie2" form; a Set-Cookie header holds exactly one.
        out.add(parseOne(h.trim(), set2));
        return out;
    }

    private static HttpCookie parseOne(String text, boolean set2) {
        int first = text.indexOf(';');
        String pair = first < 0 ? text : text.substring(0, first);
        int eq = pair.indexOf('=');
        if (eq < 0) throw new IllegalArgumentException("Invalid cookie name-value pair: " + text);
        HttpCookie c = new HttpCookie(pair.substring(0, eq).trim(), unquote(pair.substring(eq + 1).trim()));
        c.version = set2 ? 1 : 0;
        int pos = first;
        while (pos >= 0 && pos < text.length()) {
            int next = text.indexOf(';', pos + 1);
            String attr = (next < 0 ? text.substring(pos + 1) : text.substring(pos + 1, next)).trim();
            pos = next;
            if (attr.length() == 0) continue;
            int e = attr.indexOf('=');
            String key = (e < 0 ? attr : attr.substring(0, e)).trim().toLowerCase();
            String val = e < 0 ? null : unquote(attr.substring(e + 1).trim());
            if (key.equals("domain") && val != null) {
                c.domain = val.toLowerCase();
            } else if (key.equals("path") && val != null) {
                c.path = val;
            } else if (key.equals("max-age") && val != null) {
                try {
                    c.maxAge = Long.parseLong(val);
                } catch (NumberFormatException ignored) {
                    // an unreadable lifetime leaves a session cookie
                }
            } else if (key.equals("expires") && val != null && c.maxAge == -1) {
                long when = HttpDate.parse(val);
                if (when >= 0) c.maxAge = Math.max(-1L, (when - System.currentTimeMillis()) / 1000L);
                if (when >= 0 && c.maxAge == -1) c.maxAge = 0;
            } else if (key.equals("secure")) {
                c.secure = true;
            } else if (key.equals("httponly")) {
                c.httpOnly = true;
            } else if (key.equals("comment")) {
                c.comment = val;
            } else if (key.equals("port")) {
                c.portlist = val;
            } else if (key.equals("version") && val != null) {
                try {
                    c.version = Integer.parseInt(val);
                } catch (NumberFormatException ignored) {
                    // keep the default
                }
            }
        }
        return c;
    }

    private static String unquote(String s) {
        if (s.length() >= 2 && s.charAt(0) == '"' && s.charAt(s.length() - 1) == '"') {
            return s.substring(1, s.length() - 1);
        }
        return s;
    }

    public static boolean domainMatches(String domain, String host) {
        if (domain == null || host == null) return false;
        String d = domain.toLowerCase();
        String h = host.toLowerCase();
        if (d.equals(h)) return true;
        if (d.startsWith(".")) {
            return h.endsWith(d) || h.equals(d.substring(1));
        }
        return h.endsWith("." + d);
    }

    public boolean hasExpired() {
        if (maxAge == 0) return true;
        if (maxAge < 0) return false;
        return (System.currentTimeMillis() - whenCreated) / 1000L > maxAge;
    }

    public String getName() { return name; }
    public String getValue() { return value; }
    public void setValue(String newValue) { value = newValue; }
    public String getComment() { return comment; }
    public void setComment(String purpose) { comment = purpose; }
    public String getDomain() { return domain; }
    public void setDomain(String pattern) { domain = pattern == null ? null : pattern.toLowerCase(); }
    public String getPath() { return path; }
    public void setPath(String uri) { path = uri; }
    public String getPortlist() { return portlist; }
    public void setPortlist(String ports) { portlist = ports; }
    public long getMaxAge() { return maxAge; }
    public void setMaxAge(long expiry) { maxAge = expiry; }
    public boolean getSecure() { return secure; }
    public void setSecure(boolean flag) { secure = flag; }
    public boolean isHttpOnly() { return httpOnly; }
    public void setHttpOnly(boolean httpOnly) { this.httpOnly = httpOnly; }
    public int getVersion() { return version; }
    public void setVersion(int v) { version = v; }
    public boolean getDiscard() { return maxAge < 0; }
    public void setDiscard(boolean discard) { if (discard) maxAge = -1; }

    @Override
    public String toString() { return name + "=" + value; }

    @Override
    public boolean equals(Object obj) {
        if (!(obj instanceof HttpCookie)) return false;
        HttpCookie o = (HttpCookie) obj;
        return name.equalsIgnoreCase(o.name)
                && (domain == null ? o.domain == null : domain.equalsIgnoreCase(o.domain))
                && (path == null ? o.path == null : path.equals(o.path));
    }

    @Override
    public int hashCode() {
        return name.toLowerCase().hashCode() + (domain == null ? 0 : domain.toLowerCase().hashCode())
                + (path == null ? 0 : path.hashCode());
    }

    @Override
    public Object clone() {
        try {
            return super.clone();
        } catch (CloneNotSupportedException e) {
            throw new RuntimeException(e.getMessage());
        }
    }
}
