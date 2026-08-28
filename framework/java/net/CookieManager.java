package java.net;

import java.io.IOException;
import java.util.Map;
import java.util.List;
import java.util.HashMap;

public class CookieManager extends CookieHandler {
    public CookieManager() {}
    public Map<String, List<String>> get(URI uri, Map<String, List<String>> requestHeaders) throws IOException {
        return new HashMap<String, List<String>>();
    }
    public void put(URI uri, Map<String, List<String>> responseHeaders) throws IOException {}
}
