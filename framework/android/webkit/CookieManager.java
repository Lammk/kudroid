package android.webkit;

public abstract class CookieManager {
    private static CookieManager sInstance;

    protected CookieManager() {}
    public static synchronized CookieManager getInstance() {
        if (sInstance == null) sInstance = new CookieManager() {
            public void setAcceptCookie(boolean accept) {}
            public boolean acceptCookie() { return true; }
            public void setAcceptThirdPartyCookies(WebView webview, boolean accept) {}
            public boolean acceptThirdPartyCookies(WebView webview) { return true; }
            public void setCookie(String url, String value) {}
            public void setCookie(String url, String value, ValueCallback<Boolean> callback) { if (callback != null) callback.onReceiveValue(true); }
            public String getCookie(String url) { return ""; }
            public void removeAllCookies(ValueCallback<Boolean> callback) { if (callback != null) callback.onReceiveValue(true); }
            public void flush() {}
        };
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
}
