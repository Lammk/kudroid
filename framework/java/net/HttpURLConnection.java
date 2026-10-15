package java.net;

import java.io.ByteArrayOutputStream;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import javax.net.ssl.HostnameVerifier;
import javax.net.ssl.SSLSocketFactory;
import libcore.net.http.HeaderList;
import libcore.net.http.HttpEngine;

/** An HTTP client request; the exchange happens when the response is first needed. */
public abstract class HttpURLConnection extends URLConnection {
    public static final int HTTP_OK = 200;
    public static final int HTTP_CREATED = 201;
    public static final int HTTP_ACCEPTED = 202;
    public static final int HTTP_NOT_AUTHORITATIVE = 203;
    public static final int HTTP_NO_CONTENT = 204;
    public static final int HTTP_RESET = 205;
    public static final int HTTP_PARTIAL = 206;
    public static final int HTTP_MULT_CHOICE = 300;
    public static final int HTTP_MOVED_PERM = 301;
    public static final int HTTP_MOVED_TEMP = 302;
    public static final int HTTP_SEE_OTHER = 303;
    public static final int HTTP_NOT_MODIFIED = 304;
    public static final int HTTP_USE_PROXY = 305;
    public static final int HTTP_BAD_REQUEST = 400;
    public static final int HTTP_UNAUTHORIZED = 401;
    public static final int HTTP_PAYMENT_REQUIRED = 402;
    public static final int HTTP_FORBIDDEN = 403;
    public static final int HTTP_NOT_FOUND = 404;
    public static final int HTTP_BAD_METHOD = 405;
    public static final int HTTP_NOT_ACCEPTABLE = 406;
    public static final int HTTP_PROXY_AUTH = 407;
    public static final int HTTP_CLIENT_TIMEOUT = 408;
    public static final int HTTP_CONFLICT = 409;
    public static final int HTTP_GONE = 410;
    public static final int HTTP_LENGTH_REQUIRED = 411;
    public static final int HTTP_PRECON_FAILED = 412;
    public static final int HTTP_ENTITY_TOO_LARGE = 413;
    public static final int HTTP_REQ_TOO_LONG = 414;
    public static final int HTTP_UNSUPPORTED_TYPE = 415;
    public static final int HTTP_INTERNAL_ERROR = 500;
    public static final int HTTP_NOT_IMPLEMENTED = 501;
    public static final int HTTP_BAD_GATEWAY = 502;
    public static final int HTTP_UNAVAILABLE = 503;
    public static final int HTTP_GATEWAY_TIMEOUT = 504;
    public static final int HTTP_VERSION = 505;
    /** @deprecated use HTTP_ENTITY_TOO_LARGE */
    public static final int HTTP_SERVER_ERROR = 500;

    private static boolean followRedirects = true;
    private static final List<String> METHODS = Arrays.asList(
            "GET", "POST", "HEAD", "OPTIONS", "PUT", "DELETE", "TRACE");

    protected String method = "GET";
    protected int chunkLength = -1;
    protected int fixedContentLength = -1;
    protected long fixedContentLengthLong = -1;
    protected int responseCode = -1;
    protected String responseMessage;
    protected boolean instanceFollowRedirects = followRedirects;

    private ByteArrayOutputStream requestBody;
    private HttpEngine engine;
    private boolean executed;
    private IOException failure;

    protected HttpURLConnection(URL u) {
        super(u);
    }

    public static void setFollowRedirects(boolean set) { followRedirects = set; }
    public static boolean getFollowRedirects() { return followRedirects; }
    public void setInstanceFollowRedirects(boolean followRedirects) { instanceFollowRedirects = followRedirects; }
    public boolean getInstanceFollowRedirects() { return instanceFollowRedirects; }

    public void setRequestMethod(String method) throws ProtocolException {
        if (connected) throw new ProtocolException("Connection already established");
        if (!METHODS.contains(method)) {
            throw new ProtocolException("Expected one of " + METHODS + " but was " + method);
        }
        this.method = method;
    }

    public String getRequestMethod() { return method; }

    public void setFixedLengthStreamingMode(int contentLength) {
        if (connected) throw new IllegalStateException("Already connected");
        if (chunkLength != -1) throw new IllegalStateException("Already in chunked mode");
        if (contentLength < 0) throw new IllegalArgumentException("contentLength < 0");
        fixedContentLength = contentLength;
        fixedContentLengthLong = contentLength;
    }

    public void setFixedLengthStreamingMode(long contentLength) {
        if (connected) throw new IllegalStateException("Already connected");
        if (chunkLength != -1) throw new IllegalStateException("Already in chunked mode");
        if (contentLength < 0) throw new IllegalArgumentException("contentLength < 0");
        fixedContentLengthLong = contentLength;
        fixedContentLength = (int) Math.min(contentLength, (long) Integer.MAX_VALUE);
    }

    public void setChunkedStreamingMode(int chunklen) {
        if (connected) throw new IllegalStateException("Already connected");
        if (fixedContentLength != -1) throw new IllegalStateException("Already in fixed-length mode");
        chunkLength = chunklen <= 0 ? 1024 : chunklen;
    }

    public abstract void disconnect();
    public abstract boolean usingProxy();

    /** For https; the plain connection uses none. */
    protected SSLSocketFactory kuSslSocketFactory() { return null; }
    protected HostnameVerifier kuHostnameVerifier() { return null; }

    // ── exchange ─────────────────────────────────────────────────────────────

    @Override
    public void connect() throws IOException {
        // The request goes out with the first read of the response, once the body is written.
        connected = true;
    }

    private void execute() throws IOException {
        if (executed) {
            if (failure != null) throw failure;
            return;
        }
        connected = true;
        executed = true;
        final byte[] body = requestBody != null ? requestBody.toByteArray()
                : (doOutput && !method.equals("GET") && !method.equals("HEAD") ? new byte[0] : null);
        final HeaderList fields = requestFields().copy();
        if (ifModifiedSince != 0 && !fields.has("If-Modified-Since")) {
            fields.add("If-Modified-Since", libcore.net.http.HttpDate.format(ifModifiedSince));
        }
        final String effectiveMethod = (doOutput && method.equals("GET") && requestBody != null)
                ? "POST" : method;
        engine = new HttpEngine(new HttpEngine.Config() {
            public URL url() { return url; }
            public String method() { return effectiveMethod; }
            public HeaderList requestHeaders() { return fields; }
            public byte[] body() { return body; }
            public int connectTimeout() { return getConnectTimeout(); }
            public int readTimeout() { return getReadTimeout(); }
            public boolean followRedirects() { return instanceFollowRedirects; }
            public SSLSocketFactory sslSocketFactory() { return kuSslSocketFactory(); }
            public HostnameVerifier hostnameVerifier() { return kuHostnameVerifier(); }
        });
        try {
            engine.execute();
        } catch (IOException e) {
            failure = e;
            throw e;
        }
        responseCode = engine.code;
        responseMessage = engine.message;
    }

    @Override
    public OutputStream getOutputStream() throws IOException {
        if (!doOutput) throw new ProtocolException("method does not support a request body: " + method);
        if (executed) throw new ProtocolException("cannot write request body after response has been read");
        connected = true;
        if (requestBody == null) requestBody = new ByteArrayOutputStream();
        return requestBody;
    }

    @Override
    public InputStream getInputStream() throws IOException {
        if (!doInput) throw new ProtocolException("This protocol does not support input");
        execute();
        if (responseCode >= 400) {
            if (responseCode == 404 || responseCode == 410) {
                throw new FileNotFoundException(url.toString());
            }
            throw new IOException("Server returned HTTP response code: " + responseCode
                    + " for URL: " + url.toString());
        }
        return engine.body;
    }

    public InputStream getErrorStream() {
        try {
            execute();
        } catch (IOException e) {
            return null;
        }
        return responseCode >= 400 ? engine.body : null;
    }

    public int getResponseCode() throws IOException {
        execute();
        return responseCode;
    }

    public String getResponseMessage() throws IOException {
        execute();
        return responseMessage;
    }

    // ── response headers ─────────────────────────────────────────────────────

    private HeaderList responseFields() {
        try {
            execute();
        } catch (IOException e) {
            return null;
        }
        return engine.headers;
    }

    @Override
    public String getHeaderField(String name) {
        HeaderList h = responseFields();
        return h == null || name == null ? null : h.get(name);
    }

    @Override
    public String getHeaderField(int n) {
        HeaderList h = responseFields();
        return h == null || n < 0 || n >= h.size() ? null : h.value(n);
    }

    @Override
    public String getHeaderFieldKey(int n) {
        HeaderList h = responseFields();
        return h == null || n < 0 || n >= h.size() ? null : h.name(n);
    }

    @Override
    public Map<String, List<String>> getHeaderFields() {
        HeaderList h = responseFields();
        return h == null ? Collections.<String, List<String>>emptyMap() : h.toMap();
    }

    public long getHeaderFieldDate(String name, long defaultValue) {
        return super.getHeaderFieldDate(name, defaultValue);
    }

    /** The TLS session of the exchange, null for plain http. */
    protected final javax.net.ssl.SSLSession kuSession() throws IOException {
        execute();
        return engine.session;
    }

    /** Called by subclasses' disconnect(). */
    protected final void kuDisconnect() {
        if (engine != null) engine.disconnect();
    }
}
