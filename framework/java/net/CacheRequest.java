package java.net;

import java.io.OutputStream;
import java.io.IOException;

public abstract class CacheRequest {
    public CacheRequest() {}
    public abstract OutputStream getBody() throws IOException;
    public abstract void abort();
}
