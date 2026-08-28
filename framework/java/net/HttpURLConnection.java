package java.net;

import java.io.IOException;
import java.io.InputStream;
import java.io.ByteArrayInputStream;

public abstract class HttpURLConnection extends URLConnection {
    protected String method = "GET";
    protected int responseCode = -1;
    protected String responseMessage = "OK";

    public static final int HTTP_OK = 200;
    public static final int HTTP_NOT_FOUND = 404;

    protected HttpURLConnection(URL u) {
        super(u);
    }
    public void setRequestMethod(String method) throws ProtocolException {
        this.method = method;
    }
    public String getRequestMethod() { return method; }
    public int getResponseCode() throws IOException { return 200; }
    public String getResponseMessage() throws IOException { return "OK"; }
    public abstract void disconnect();
    public abstract boolean usingProxy();
    public InputStream getErrorStream() { return null; }
}
