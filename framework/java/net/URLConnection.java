package java.net;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import libcore.net.http.HeaderList;
import libcore.net.http.HttpDate;

public abstract class URLConnection {
    protected URL url;
    protected boolean doInput = true;
    protected boolean doOutput = false;
    protected boolean allowUserInteraction = defaultAllowUserInteraction;
    protected boolean useCaches = defaultUseCaches;
    protected long ifModifiedSince = 0;
    protected boolean connected = false;

    private static boolean defaultAllowUserInteraction = false;
    private static boolean defaultUseCaches = true;
    private int connectTimeout;
    private int readTimeout;
    private final HeaderList requests = new HeaderList();

    protected URLConnection(URL url) {
        this.url = url;
    }

    public abstract void connect() throws IOException;

    public URL getURL() { return url; }

    // ── response header accessors; the protocol subclasses supply getHeaderField ──────────

    public String getHeaderField(String name) { return null; }
    public String getHeaderField(int n) { return null; }
    public String getHeaderFieldKey(int n) { return null; }
    public Map<String, List<String>> getHeaderFields() { return Collections.emptyMap(); }

    public int getHeaderFieldInt(String name, int defaultValue) {
        try {
            return Integer.parseInt(getHeaderField(name).trim());
        } catch (RuntimeException e) {
            return defaultValue;
        }
    }

    public long getHeaderFieldLong(String name, long defaultValue) {
        try {
            return Long.parseLong(getHeaderField(name).trim());
        } catch (RuntimeException e) {
            return defaultValue;
        }
    }

    public long getHeaderFieldDate(String name, long defaultValue) {
        long t = HttpDate.parse(getHeaderField(name));
        return t < 0 ? defaultValue : t;
    }

    public int getContentLength() {
        long l = getContentLengthLong();
        return l > Integer.MAX_VALUE ? -1 : (int) l;
    }

    public long getContentLengthLong() { return getHeaderFieldLong("content-length", -1); }
    public String getContentType() { return getHeaderField("content-type"); }
    public String getContentEncoding() { return getHeaderField("content-encoding"); }
    public long getExpiration() { return getHeaderFieldDate("expires", 0); }
    public long getDate() { return getHeaderFieldDate("date", 0); }
    public long getLastModified() { return getHeaderFieldDate("last-modified", 0); }

    // ── streams and content ─────────────────────────────────────────────────

    public InputStream getInputStream() throws IOException {
        throw new UnknownServiceException("Does not support reading");
    }

    public OutputStream getOutputStream() throws IOException {
        throw new UnknownServiceException("Does not support writing");
    }

    public Object getContent() throws IOException {
        return getInputStream();
    }

    public Object getContent(Class[] types) throws IOException {
        return getInputStream();
    }

    // ── request settings ────────────────────────────────────────────────────

    public void setDoInput(boolean doinput) {
        checkNotConnected();
        this.doInput = doinput;
    }

    public boolean getDoInput() { return doInput; }

    public void setDoOutput(boolean dooutput) {
        checkNotConnected();
        this.doOutput = dooutput;
    }

    public boolean getDoOutput() { return doOutput; }

    public void setAllowUserInteraction(boolean allowuserinteraction) {
        checkNotConnected();
        this.allowUserInteraction = allowuserinteraction;
    }

    public boolean getAllowUserInteraction() { return allowUserInteraction; }

    public void setUseCaches(boolean usecaches) {
        checkNotConnected();
        this.useCaches = usecaches;
    }

    public boolean getUseCaches() { return useCaches; }

    public void setIfModifiedSince(long ifmodifiedsince) {
        checkNotConnected();
        this.ifModifiedSince = ifmodifiedsince;
    }

    public long getIfModifiedSince() { return ifModifiedSince; }

    public static void setDefaultAllowUserInteraction(boolean defaultallowuserinteraction) {
        defaultAllowUserInteraction = defaultallowuserinteraction;
    }

    public static boolean getDefaultAllowUserInteraction() { return defaultAllowUserInteraction; }

    public void setDefaultUseCaches(boolean defaultusecaches) { defaultUseCaches = defaultusecaches; }
    public boolean getDefaultUseCaches() { return defaultUseCaches; }

    public void setConnectTimeout(int timeout) {
        if (timeout < 0) throw new IllegalArgumentException("timeout < 0");
        connectTimeout = timeout;
    }

    public int getConnectTimeout() { return connectTimeout; }

    public void setReadTimeout(int timeout) {
        if (timeout < 0) throw new IllegalArgumentException("timeout < 0");
        readTimeout = timeout;
    }

    public int getReadTimeout() { return readTimeout; }

    public void setRequestProperty(String key, String value) {
        checkNotConnected();
        if (key == null) throw new NullPointerException("field == null");
        if (value == null) {
            requests.remove(key);
        } else {
            requests.set(key, value);
        }
    }

    public void addRequestProperty(String key, String value) {
        checkNotConnected();
        if (key == null) throw new NullPointerException("field == null");
        if (value != null) requests.add(key, value);
    }

    public String getRequestProperty(String key) {
        return key == null ? null : requests.get(key);
    }

    public Map<String, List<String>> getRequestProperties() {
        return requests.toMap();
    }

    /** The request fields as set so far, for the protocol implementation. */
    protected final HeaderList requestFields() { return requests; }

    private void checkNotConnected() {
        if (connected) throw new IllegalStateException("Already connected");
    }

    // ── content types ───────────────────────────────────────────────────────

    public static String guessContentTypeFromName(String fname) {
        if (fname == null) return null;
        int dot = fname.lastIndexOf('.');
        if (dot < 0) return null;
        String ext = fname.substring(dot + 1).toLowerCase();
        if (ext.equals("html") || ext.equals("htm")) return "text/html";
        if (ext.equals("txt")) return "text/plain";
        if (ext.equals("css")) return "text/css";
        if (ext.equals("js")) return "application/javascript";
        if (ext.equals("json")) return "application/json";
        if (ext.equals("xml")) return "application/xml";
        if (ext.equals("png")) return "image/png";
        if (ext.equals("jpg") || ext.equals("jpeg")) return "image/jpeg";
        if (ext.equals("gif")) return "image/gif";
        if (ext.equals("webp")) return "image/webp";
        if (ext.equals("svg")) return "image/svg+xml";
        if (ext.equals("mp3")) return "audio/mpeg";
        if (ext.equals("mp4")) return "video/mp4";
        if (ext.equals("pdf")) return "application/pdf";
        if (ext.equals("zip")) return "application/zip";
        return null;
    }

    public static String guessContentTypeFromStream(InputStream is) throws IOException {
        return null;
    }

    @Override
    public String toString() {
        return getClass().getName() + ":" + url;
    }
}
