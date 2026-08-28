package java.net;

import java.io.InputStream;
import java.io.OutputStream;
import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.util.Map;
import java.util.List;
import java.util.HashMap;

public abstract class URLConnection {
    protected URL url;
    protected boolean doInput = true;
    protected boolean doOutput = false;
    protected boolean connected = false;
    private final Map<String, List<String>> requests = new HashMap<String, List<String>>();

    protected URLConnection(URL url) {
        this.url = url;
    }
    public abstract void connect() throws IOException;
    public URL getURL() { return url; }
    public int getContentLength() { return -1; }
    public long getContentLengthLong() { return -1; }
    public String getContentType() { return "application/octet-stream"; }
    public String getContentEncoding() { return null; }
    public InputStream getInputStream() throws IOException {
        return new ByteArrayInputStream(new byte[0]);
    }
    public OutputStream getOutputStream() throws IOException {
        return new ByteArrayOutputStream();
    }
    public void setDoInput(boolean doinput) { this.doInput = doinput; }
    public boolean getDoInput() { return doInput; }
    public void setDoOutput(boolean dooutput) { this.doOutput = dooutput; }
    public boolean getDoOutput() { return doOutput; }
    public void setConnectTimeout(int timeout) {}
    public int getConnectTimeout() { return 0; }
    public void setReadTimeout(int timeout) {}
    public int getReadTimeout() { return 0; }
    public void setRequestProperty(String key, String value) {}
    public void addRequestProperty(String key, String value) {}
    public String getRequestProperty(String key) { return null; }
    public Map<String, List<String>> getHeaderFields() { return requests; }
    public String getHeaderField(String name) { return null; }
}
