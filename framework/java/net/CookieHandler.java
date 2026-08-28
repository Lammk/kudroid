package java.net;

import java.io.IOException;
import java.util.Map;
import java.util.List;

public abstract class CookieHandler {
    private static CookieHandler cookieHandler;
    public static synchronized CookieHandler getDefault() { return cookieHandler; }
    public static synchronized void setDefault(CookieHandler cHandler) { cookieHandler = cHandler; }
    public abstract Map<String, List<String>> get(URI uri, Map<String, List<String>> requestHeaders) throws IOException;
    public abstract void put(URI uri, Map<String, List<String>> responseHeaders) throws IOException;
}
