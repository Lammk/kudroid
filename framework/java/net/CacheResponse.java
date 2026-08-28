package java.net;

import java.io.InputStream;
import java.io.IOException;
import java.util.Map;
import java.util.List;

public abstract class CacheResponse {
    public CacheResponse() {}
    public abstract Map<String, List<String>> getHeaders() throws IOException;
    public abstract InputStream getBody() throws IOException;
}
