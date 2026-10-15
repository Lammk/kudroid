package java.net;

import java.io.IOException;
import java.util.Map;
import java.util.List;

public abstract class ResponseCache {
    private static ResponseCache theResponseCache;
    public static synchronized ResponseCache getDefault() { return theResponseCache; }
    public static synchronized void setDefault(ResponseCache responseCache) { theResponseCache = responseCache; }
    public abstract CacheResponse get(URI uri, String rqstMethod, Map<String, List<String>> rqstHeaders) throws IOException;
    public abstract CacheRequest put(URI uri, URLConnection conn) throws IOException;
}
