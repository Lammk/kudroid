package android.webkit;

import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.Map;

/**
 * In-memory CookieManager. The host web view keeps its own cookie store for
 * actual page loads; this implementation serves the android.webkit API
 * surface so apps that set/read cookies programmatically (a common hybrid
 * runtime pattern) observe consistent values within the process. Cookies
 * are not persisted across process restarts yet.
 */
public abstract class CookieManager {
    private static CookieManager sInstance;

    protected CookieManager() {}

    public static synchronized CookieManager getInstance() {
        if (sInstance == null) sInstance = new InMemoryCookieManager();
        return sInstance;
    }

    public abstract void setAcceptCookie(boolean accept);
    public abstract boolean acceptCookie();
    public abstract void setAcceptThirdPartyCookies(WebView webview, boolean accept);
    public abstract boolean acceptThirdPartyCookies(WebView webview);
    public abstract void setCookie(String url, String value);
    public abstract void setCookie(String url, String value, ValueCallback<Boolean> callback);
    public abstract String getCookie(String url);
    public abstract void removeAllCookies(ValueCallback<Boolean> callback);
    public abstract void flush();

    public void removeSessionCookies(ValueCallback<Boolean> callback) {
        removeAllCookies(callback);
    }

    public boolean hasCookies() { return false; }

    private static final class InMemoryCookieManager extends CookieManager {
        private boolean mAccept = true;
        private boolean mAcceptThirdParty = true;
        // host -> (name -> value), insertion ordered for stable output
        private final Map<String, LinkedHashMap<String, String>> mByHost = new HashMap<>();

        @Override public synchronized void setAcceptCookie(boolean accept) { mAccept = accept; }
        @Override public synchronized boolean acceptCookie() { return mAccept; }
        @Override public synchronized void setAcceptThirdPartyCookies(WebView webview, boolean accept) {
            mAcceptThirdParty = accept;
        }
        @Override public synchronized boolean acceptThirdPartyCookies(WebView webview) {
            return mAcceptThirdParty;
        }

        @Override public void setCookie(String url, String value) {
            setCookie(url, value, null);
        }

        @Override public synchronized void setCookie(String url, String value,
                                                     ValueCallback<Boolean> callback) {
            boolean ok = false;
            if (mAccept && url != null && value != null) {
                String host = hostOf(url);
                // value may carry several cookies separated by newlines when
                // it comes from a Set-Cookie dump; each line's first pair is
                // the cookie, the rest are attributes we mostly ignore.
                for (String line : value.split("\n")) {
                    String pair = line.split(";", 2)[0].trim();
                    int eq = pair.indexOf('=');
                    if (eq > 0) {
                        LinkedHashMap<String, String> jar = mByHost.get(host);
                        if (jar == null) {
                            jar = new LinkedHashMap<>();
                            mByHost.put(host, jar);
                        }
                        jar.put(pair.substring(0, eq).trim(), pair.substring(eq + 1).trim());
                        ok = true;
                    }
                }
            }
            if (callback != null) callback.onReceiveValue(ok);
        }

        @Override public synchronized String getCookie(String url) {
            if (url == null) return "";
            LinkedHashMap<String, String> jar = mByHost.get(hostOf(url));
            if (jar == null || jar.isEmpty()) return "";
            StringBuilder sb = new StringBuilder();
            for (Map.Entry<String, String> e : jar.entrySet()) {
                if (sb.length() > 0) sb.append("; ");
                sb.append(e.getKey()).append('=').append(e.getValue());
            }
            return sb.toString();
        }

        @Override public synchronized void removeAllCookies(ValueCallback<Boolean> callback) {
            boolean had = !mByHost.isEmpty();
            mByHost.clear();
            if (callback != null) callback.onReceiveValue(had);
        }

        @Override public void flush() {}

        @Override public synchronized boolean hasCookies() {
            return !mByHost.isEmpty();
        }

        private static String hostOf(String url) {
            // Framework Uri has no getHost(); extract it directly.
            String u = url;
            int scheme = u.indexOf("://");
            if (scheme >= 0) u = u.substring(scheme + 3);
            int slash = u.indexOf('/');
            if (slash >= 0) u = u.substring(0, slash);
            int colon = u.indexOf(':');
            if (colon >= 0) u = u.substring(0, colon);
            return u.isEmpty() ? url : u;
        }
    }
}
