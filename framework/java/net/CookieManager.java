package java.net;

import java.io.IOException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/** Keeps cookies in memory and applies them to requests as the default CookieHandler. */
public class CookieManager extends CookieHandler {
    private CookiePolicy policy;
    private final CookieStore store;

    public CookieManager() {
        this(null, null);
    }

    public CookieManager(CookieStore store, CookiePolicy cookiePolicy) {
        this.policy = cookiePolicy != null ? cookiePolicy : CookiePolicy.ACCEPT_ORIGINAL_SERVER;
        this.store = store != null ? store : new MemoryStore();
    }

    public void setCookiePolicy(CookiePolicy cookiePolicy) {
        if (cookiePolicy != null) policy = cookiePolicy;
    }

    public CookieStore getCookieStore() { return store; }

    @Override
    public Map<String, List<String>> get(URI uri, Map<String, List<String>> requestHeaders) throws IOException {
        if (uri == null || requestHeaders == null) throw new IllegalArgumentException("Argument is null");
        Map<String, List<String>> out = new HashMap<String, List<String>>();
        String host = uri.getHost();
        String path = uri.getPath();
        if (path == null || path.length() == 0) path = "/";
        boolean secure = "https".equalsIgnoreCase(uri.getScheme());
        StringBuilder sb = new StringBuilder();
        for (HttpCookie c : store.getCookies()) {
            if (c.hasExpired()) continue;
            if (c.getSecure() && !secure) continue;
            if (!domainAccepts(c, host)) continue;
            if (!pathMatches(c.getPath(), path)) continue;
            if (sb.length() > 0) sb.append("; ");
            sb.append(c.getName()).append('=').append(c.getValue());
        }
        if (sb.length() > 0) out.put("Cookie", Collections.singletonList(sb.toString()));
        return Collections.unmodifiableMap(out);
    }

    @Override
    public void put(URI uri, Map<String, List<String>> responseHeaders) throws IOException {
        if (uri == null || responseHeaders == null) return;
        for (Map.Entry<String, List<String>> e : responseHeaders.entrySet()) {
            String key = e.getKey();
            if (key == null) continue;
            if (!key.equalsIgnoreCase("Set-Cookie") && !key.equalsIgnoreCase("Set-Cookie2")) continue;
            for (String header : e.getValue()) {
                List<HttpCookie> parsed;
                try {
                    parsed = HttpCookie.parse(header);
                } catch (IllegalArgumentException ignored) {
                    continue;  // a malformed cookie is dropped, not fatal
                }
                for (HttpCookie c : parsed) {
                    if (c.getDomain() == null) {
                        c.setDomain(uri.getHost());
                        c.hostOnly = true;
                    }
                    if (c.getPath() == null) c.setPath(defaultPath(uri.getPath()));
                    if (policy.shouldAccept(uri, c)) store.add(uri, c);
                }
            }
        }
    }

    private static boolean domainAccepts(HttpCookie c, String host) {
        if (host == null) return false;
        if (c.hostOnly) return host.equalsIgnoreCase(c.getDomain());
        return HttpCookie.domainMatches(c.getDomain(), host);
    }

    private static boolean pathMatches(String cookiePath, String requestPath) {
        if (cookiePath == null || cookiePath.length() == 0 || cookiePath.equals("/")) return true;
        if (!requestPath.startsWith(cookiePath)) return false;
        return requestPath.length() == cookiePath.length() || cookiePath.endsWith("/")
                || requestPath.charAt(cookiePath.length()) == '/';
    }

    private static String defaultPath(String requestPath) {
        if (requestPath == null || requestPath.length() == 0 || requestPath.charAt(0) != '/') return "/";
        int last = requestPath.lastIndexOf('/');
        return last <= 0 ? "/" : requestPath.substring(0, last);
    }

    /** A cookie jar that replaces a cookie with the same name, domain and path. */
    private static final class MemoryStore implements CookieStore {
        private final ArrayList<HttpCookie> cookies = new ArrayList<HttpCookie>();
        private final ArrayList<URI> uris = new ArrayList<URI>();

        public synchronized void add(URI uri, HttpCookie cookie) {
            if (cookie == null) throw new NullPointerException("cookie == null");
            cookies.remove(cookie);
            if (cookie.getMaxAge() != 0) {
                cookies.add(cookie);
                if (uri != null && !uris.contains(uri)) uris.add(uri);
            }
        }

        public synchronized List<HttpCookie> get(URI uri) {
            ArrayList<HttpCookie> out = new ArrayList<HttpCookie>();
            for (HttpCookie c : cookies) {
                if (!c.hasExpired() && uri != null && domainAccepts(c, uri.getHost())) out.add(c);
            }
            return out;
        }

        public synchronized List<HttpCookie> getCookies() {
            ArrayList<HttpCookie> live = new ArrayList<HttpCookie>();
            for (int i = cookies.size() - 1; i >= 0; i--) {
                if (cookies.get(i).hasExpired()) cookies.remove(i);
            }
            live.addAll(cookies);
            return Collections.unmodifiableList(live);
        }

        public synchronized List<URI> getURIs() {
            return Collections.unmodifiableList(new ArrayList<URI>(uris));
        }

        public synchronized boolean remove(URI uri, HttpCookie cookie) {
            return cookies.remove(cookie);
        }

        public synchronized boolean removeAll() {
            boolean any = !cookies.isEmpty();
            cookies.clear();
            uris.clear();
            return any;
        }
    }
}
