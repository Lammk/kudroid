package libcore.net.http;

import java.io.ByteArrayOutputStream;
import java.io.EOFException;
import java.io.IOException;
import java.io.InputStream;
import java.net.CookieHandler;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.ProtocolException;
import java.net.Socket;
import java.net.SocketTimeoutException;
import java.net.URI;
import java.net.URL;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.zip.GZIPInputStream;
import java.util.zip.InflaterInputStream;
import javax.net.ssl.HostnameVerifier;
import javax.net.ssl.HttpsURLConnection;
import javax.net.ssl.SSLPeerUnverifiedException;
import javax.net.ssl.SSLSession;
import javax.net.ssl.SSLSocket;
import javax.net.ssl.SSLSocketFactory;

/**
 * An HTTP/1.1 client: one request (with redirects) over a pooled or fresh connection.
 *
 * It owns the wire format only. The URLConnection classes that front it supply the request
 * through {@link Config} and read the outcome from the public fields afterwards.
 */
public final class HttpEngine {
    /** What the caller asks for. */
    public interface Config {
        URL url();
        String method();
        HeaderList requestHeaders();
        /** The request body, or null. */
        byte[] body();
        int connectTimeout();
        int readTimeout();
        boolean followRedirects();
        /** For https; null selects the platform default. */
        SSLSocketFactory sslSocketFactory();
        HostnameVerifier hostnameVerifier();
    }

    private static final int MAX_REDIRECTS = 20;
    private static final int MAX_LINE = 64 * 1024;
    private static final long IDLE_MILLIS = 60 * 1000L;
    private static final int MAX_IDLE = 8;

    private static final ArrayList<Conn> sIdle = new ArrayList<Conn>();

    // ── outcome ──────────────────────────────────────────────────────────────

    public int code = -1;
    public String message = "";
    public HeaderList headers = new HeaderList();
    /** The URL of the final response, after any redirects. */
    public URL url;
    public InputStream body;
    public SSLSession session;

    private final Config config;
    private Body live;

    public HttpEngine(Config config) {
        this.config = config;
    }

    // ── a connection ─────────────────────────────────────────────────────────

    private static final class Conn {
        final String key;
        final Socket socket;
        final Input in;
        final java.io.OutputStream out;
        SSLSession session;
        long idleSince;
        boolean reused;

        Conn(String key, Socket socket) throws IOException {
            this.key = key;
            this.socket = socket;
            this.in = new Input(socket.getInputStream());
            this.out = socket.getOutputStream();
        }

        void close() {
            try {
                socket.close();
            } catch (IOException ignored) {
                // closing
            }
        }
    }

    /** Buffered input that keeps bytes read ahead for the next response. */
    private static final class Input {
        private final InputStream raw;
        private final byte[] buf = new byte[8192];
        private int pos;
        private int limit;

        Input(InputStream raw) { this.raw = raw; }

        private boolean fill() throws IOException {
            if (pos < limit) return true;
            int n = raw.read(buf, 0, buf.length);
            if (n <= 0) return false;
            pos = 0;
            limit = n;
            return true;
        }

        int read() throws IOException {
            return fill() ? (buf[pos++] & 0xff) : -1;
        }

        int read(byte[] b, int off, int len) throws IOException {
            if (len == 0) return 0;
            if (!fill()) return -1;
            int n = Math.min(len, limit - pos);
            System.arraycopy(buf, pos, b, off, n);
            pos += n;
            return n;
        }

        /** A line without its terminator, or null at end of stream. */
        String readLine() throws IOException {
            StringBuilder sb = new StringBuilder();
            for (;;) {
                int c = read();
                if (c < 0) return sb.length() == 0 ? null : sb.toString();
                if (c == '\n') {
                    int n = sb.length();
                    if (n > 0 && sb.charAt(n - 1) == '\r') sb.setLength(n - 1);
                    return sb.toString();
                }
                sb.append((char) c);
                if (sb.length() > MAX_LINE) throw new ProtocolException("header line too long");
            }
        }

        boolean hasBuffered() { return pos < limit; }
    }

    // ── execution ────────────────────────────────────────────────────────────

    /** Sends the request, follows redirects, and leaves the final response in the fields. */
    public void execute() throws IOException {
        URL cur = config.url();
        String method = config.method();
        byte[] reqBody = config.body();
        HeaderList reqHeaders = config.requestHeaders().copy();
        int redirects = 0;

        for (;;) {
            exchange(cur, method, reqHeaders, reqBody);
            url = cur;
            if (!config.followRedirects() || !isRedirect(code)) break;
            String location = headers.get("Location");
            if (location == null) break;
            URL next;
            try {
                next = new URL(cur, location);
            } catch (java.net.MalformedURLException e) {
                break;
            }
            String proto = next.getProtocol();
            if (!proto.equals("http") && !proto.equals("https")) break;
            if (++redirects > MAX_REDIRECTS) {
                abandon();
                throw new ProtocolException("Too many follow-up requests: " + redirects);
            }
            boolean switchToGet = !method.equals("GET") && !method.equals("HEAD")
                    && (code == 303 || ((code == 301 || code == 302) && method.equals("POST")));
            abandon();
            if (switchToGet) {
                method = "GET";
                reqBody = null;
                reqHeaders.remove("Content-Length");
                reqHeaders.remove("Content-Type");
                reqHeaders.remove("Transfer-Encoding");
            }
            if (!sameOrigin(cur, next)) reqHeaders.remove("Authorization");
            cur = next;
        }
    }

    private static boolean isRedirect(int code) {
        return code == 301 || code == 302 || code == 303 || code == 307 || code == 308;
    }

    private static boolean sameOrigin(URL a, URL b) {
        return a.getProtocol().equals(b.getProtocol())
                && a.getHost().equalsIgnoreCase(b.getHost())
                && portOf(a) == portOf(b);
    }

    private static int portOf(URL u) {
        return u.getPort() != -1 ? u.getPort() : u.getDefaultPort();
    }

    /** Drops the current response without reusing its connection. */
    private void abandon() {
        if (live != null) {
            try {
                live.close();
            } catch (IOException ignored) {
                // dropping it anyway
            }
            live = null;
        }
        body = null;
    }

    // ── one request/response ─────────────────────────────────────────────────

    private void exchange(URL target, String method, HeaderList userHeaders, byte[] reqBody)
            throws IOException {
        boolean secure = target.getProtocol().equals("https");
        String key = (secure ? "https://" : "http://") + target.getHost().toLowerCase() + ":" + portOf(target)
                + (secure ? "#" + System.identityHashCode(config.sslSocketFactory()) : "");
        byte[] request = buildRequest(target, method, userHeaders, reqBody);

        boolean mayInflate = !userHeaders.has("Accept-Encoding");
        for (int attempt = 0; ; attempt++) {
            Conn c = attempt == 0 ? takeIdle(key) : null;
            if (c == null) c = open(target, secure, key);
            try {
                c.out.write(request);
                c.out.flush();
                readResponse(c, target, method, mayInflate);
                return;
            } catch (IOException e) {
                c.close();
                // A pooled connection the server already closed fails before any response byte;
                // anything else is real.
                if (c.reused && attempt == 0 && !(e instanceof SocketTimeoutException)) continue;
                throw e;
            }
        }
    }

    private byte[] buildRequest(URL target, String method, HeaderList user, byte[] reqBody)
            throws IOException {
        HeaderList h = user.copy();
        String file = target.getFile();
        if (file.length() == 0) file = "/";
        else if (file.charAt(0) == '?') file = "/" + file;

        if (!h.has("Host")) {
            int port = target.getPort();
            String host = target.getHost();
            h.add("Host", port != -1 && port != target.getDefaultPort() ? host + ":" + port : host);
        }
        if (!h.has("User-Agent")) h.add("User-Agent", userAgent());
        if (!h.has("Accept-Encoding")) h.add("Accept-Encoding", "gzip");
        if (!h.has("Connection")) h.add("Connection", "keep-alive");
        if (reqBody != null) {
            if (!h.has("Content-Length") && !h.has("Transfer-Encoding")) {
                h.add("Content-Length", String.valueOf(reqBody.length));
            }
            if (!h.has("Content-Type")) h.add("Content-Type", "application/x-www-form-urlencoded");
        } else if (method.equals("POST") || method.equals("PUT")) {
            if (!h.has("Content-Length")) h.add("Content-Length", "0");
        }
        String userInfo = target.getUserInfo();
        if (userInfo != null && !h.has("Authorization")) {
            h.add("Authorization", "Basic " + java.util.Base64.getEncoder().encodeToString(userInfo.getBytes()));
        }
        addCookies(target, h);

        StringBuilder sb = new StringBuilder(256);
        sb.append(method).append(' ').append(file).append(" HTTP/1.1\r\n");
        for (int i = 0; i < h.size(); i++) {
            sb.append(h.name(i)).append(": ").append(h.value(i)).append("\r\n");
        }
        sb.append("\r\n");
        byte[] head = sb.toString().getBytes();
        if (reqBody == null || reqBody.length == 0) return head;
        byte[] all = new byte[head.length + reqBody.length];
        System.arraycopy(head, 0, all, 0, head.length);
        System.arraycopy(reqBody, 0, all, head.length, reqBody.length);
        return all;
    }

    private static String userAgent() {
        String ua = System.getProperty("http.agent");
        return ua != null ? ua : "Dalvik/2.1.0 (Linux; U; Android 13)";
    }

    private void addCookies(URL target, HeaderList h) {
        CookieHandler handler = CookieHandler.getDefault();
        if (handler == null) return;
        try {
            URI uri = target.toURI();
            Map<String, List<String>> existing = h.toMap();
            Map<String, List<String>> add = handler.get(uri, existing);
            if (add == null) return;
            for (Map.Entry<String, List<String>> e : add.entrySet()) {
                String name = e.getKey();
                if (name == null || e.getValue().isEmpty()) continue;
                if (!name.equalsIgnoreCase("Cookie") && !name.equalsIgnoreCase("Cookie2")) continue;
                StringBuilder sb = new StringBuilder();
                for (String v : e.getValue()) {
                    if (sb.length() > 0) sb.append("; ");
                    sb.append(v);
                }
                h.set(name, sb.toString());
            }
        } catch (Exception ignored) {
            // cookies are best-effort
        }
    }

    private void storeCookies(URL target, HeaderList responseHeaders) {
        CookieHandler handler = CookieHandler.getDefault();
        if (handler == null) return;
        try {
            handler.put(target.toURI(), responseHeaders.toMap());
        } catch (Exception ignored) {
            // cookies are best-effort
        }
    }

    // ── connecting ───────────────────────────────────────────────────────────

    private Conn open(URL target, boolean secure, String key) throws IOException {
        String host = target.getHost();
        int port = portOf(target);
        InetAddress[] addrs = InetAddress.getAllByName(host);
        Socket s = null;
        IOException last = null;
        // Only the TCP connect falls through to the next address; a TLS failure is the answer.
        for (int i = 0; i < addrs.length && s == null; i++) {
            Socket candidate = new Socket();
            try {
                candidate.connect(new InetSocketAddress(addrs[i], port), config.connectTimeout());
                s = candidate;
            } catch (IOException e) {
                last = e;
                try {
                    candidate.close();
                } catch (IOException ignored) {
                    // next address
                }
            }
        }
        if (s == null) throw last != null ? last : new java.net.UnknownHostException(host);
        try {
            s.setTcpNoDelay(true);
            s.setSoTimeout(config.readTimeout());
            Socket top = s;
            SSLSession sess = null;
            if (secure) {
                SSLSocket ssl = handshake(s, host, port);
                sess = ssl.getSession();
                top = ssl;
            }
            Conn c = new Conn(key, top);
            c.session = sess;
            return c;
        } catch (IOException e) {
            try {
                s.close();
            } catch (IOException ignored) {
                // reporting the original failure
            }
            throw e;
        }
    }

    private SSLSocket handshake(Socket raw, String host, int port) throws IOException {
        SSLSocketFactory f = config.sslSocketFactory();
        if (f == null) f = (SSLSocketFactory) SSLSocketFactory.getDefault();
        SSLSocket ssl = (SSLSocket) f.createSocket(raw, host, port, true);
        try {
            ssl.startHandshake();
            HostnameVerifier v = config.hostnameVerifier();
            if (v == null) v = HttpsURLConnection.getDefaultHostnameVerifier();
            if (!v.verify(host, ssl.getSession())) {
                throw new SSLPeerUnverifiedException("Hostname " + host + " not verified");
            }
        } catch (IOException e) {
            try {
                ssl.close();
            } catch (IOException ignored) {
                // reporting the handshake failure
            }
            throw e;
        }
        return ssl;
    }

    // ── pool ─────────────────────────────────────────────────────────────────

    private static Conn takeIdle(String key) {
        long now = System.currentTimeMillis();
        synchronized (sIdle) {
            for (int i = sIdle.size() - 1; i >= 0; i--) {
                Conn c = sIdle.get(i);
                if (now - c.idleSince > IDLE_MILLIS) {
                    sIdle.remove(i);
                    c.close();
                    continue;
                }
                if (c.key.equals(key)) {
                    sIdle.remove(i);
                    // Readable while idle means the server closed it (or sent junk).
                    if (c.in.hasBuffered() || c.socket.isClosed() || c.socket.kuPeerClosed()) {
                        c.close();
                        continue;
                    }
                    c.reused = true;
                    return c;
                }
            }
        }
        return null;
    }

    private static void release(Conn c) {
        c.idleSince = System.currentTimeMillis();
        c.reused = false;
        synchronized (sIdle) {
            sIdle.add(c);
            while (sIdle.size() > MAX_IDLE) {
                sIdle.remove(0).close();
            }
        }
    }

    // ── response ─────────────────────────────────────────────────────────────

    private void readResponse(Conn c, URL target, String method, boolean mayInflate)
            throws IOException {
        String status;
        for (;;) {
            status = c.in.readLine();
            if (status == null) throw new java.net.SocketException("Unexpected end of stream");
            if (status.length() > 0) break;
        }
        int space = status.indexOf(' ');
        if (!status.startsWith("HTTP/") || space < 0) {
            throw new ProtocolException("Unexpected status line: " + status);
        }
        String version = status.substring(0, space);
        int codeEnd = status.indexOf(' ', space + 1);
        String codeText = codeEnd < 0 ? status.substring(space + 1) : status.substring(space + 1, codeEnd);
        int parsedCode;
        try {
            parsedCode = Integer.parseInt(codeText.trim());
        } catch (NumberFormatException e) {
            throw new ProtocolException("Unexpected status line: " + status);
        }
        String msg = codeEnd < 0 ? "" : status.substring(codeEnd + 1);

        HeaderList h = new HeaderList();
        h.add(null, status);
        readHeaders(c.in, h);

        if (parsedCode == 100) {
            // Interim response: the real one follows.
            readResponse(c, target, method, mayInflate);
            return;
        }
        this.code = parsedCode;
        this.message = msg;
        this.session = c.session;

        boolean http10 = version.equals("HTTP/1.0");
        String connection = h.get("Connection");
        boolean keepAlive = http10 ? (connection != null && connection.equalsIgnoreCase("keep-alive"))
                : (connection == null || !connection.equalsIgnoreCase("close"));

        Body raw;
        boolean noBody = method.equals("HEAD") || (parsedCode >= 100 && parsedCode < 200)
                || parsedCode == 204 || parsedCode == 304;
        String te = h.get("Transfer-Encoding");
        String len = h.get("Content-Length");
        if (noBody) {
            raw = new Fixed(c, 0, keepAlive);
        } else if (te != null && te.equalsIgnoreCase("chunked")) {
            raw = new Chunked(c, keepAlive, h);
        } else if (len != null) {
            long n;
            try {
                n = Long.parseLong(len.trim());
            } catch (NumberFormatException e) {
                throw new ProtocolException("Invalid Content-Length: " + len);
            }
            raw = new Fixed(c, n, keepAlive);
        } else {
            raw = new UntilClose(c);
        }

        live = raw;
        String enc = h.get("Content-Encoding");
        InputStream decoded = raw;
        if (mayInflate && !noBody && enc != null) {
            if (enc.equalsIgnoreCase("gzip")) {
                decoded = new Lazy(raw, true);
                h.remove("Content-Encoding");
                h.remove("Content-Length");
            } else if (enc.equalsIgnoreCase("deflate")) {
                decoded = new Lazy(raw, false);
                h.remove("Content-Encoding");
                h.remove("Content-Length");
            }
        }
        this.headers = h;
        this.body = decoded;
        storeCookies(target, h);
    }

    private static void readHeaders(Input in, HeaderList h) throws IOException {
        String last = null;
        for (;;) {
            String line = in.readLine();
            if (line == null) throw new EOFException("Unexpected end of headers");
            if (line.length() == 0) return;
            if ((line.charAt(0) == ' ' || line.charAt(0) == '\t') && h.size() > 1) {
                // Obsolete line folding: continues the previous field.
                int i = h.size() - 1;
                String name = h.name(i);
                String joined = h.value(i) + " " + line.trim();
                h.remove(name);
                h.add(name, joined);
                continue;
            }
            int colon = line.indexOf(':');
            if (colon <= 0) continue;
            h.add(line.substring(0, colon).trim(), line.substring(colon + 1).trim());
        }
    }

    // ── body streams ─────────────────────────────────────────────────────────

    private abstract static class Body extends InputStream {
        final Conn conn;
        boolean done;
        boolean closed;

        Body(Conn conn) { this.conn = conn; }

        /** The body ended cleanly: reuse the connection, or close it. */
        void finish(boolean reusable) {
            if (done) return;
            done = true;
            if (reusable) release(conn); else conn.close();
        }

        @Override
        public int read() throws IOException {
            byte[] one = new byte[1];
            int n = read(one, 0, 1);
            return n < 0 ? -1 : (one[0] & 0xff);
        }

        @Override
        public void close() throws IOException {
            if (closed) return;
            closed = true;
            if (!done) {
                done = true;
                conn.close();
            }
        }
    }

    private static final class Fixed extends Body {
        private long remaining;
        private final boolean keepAlive;

        Fixed(Conn conn, long length, boolean keepAlive) {
            super(conn);
            this.remaining = length;
            this.keepAlive = keepAlive;
            if (length == 0) finish(keepAlive);
        }

        @Override
        public int read(byte[] b, int off, int len) throws IOException {
            if (closed) throw new IOException("stream closed");
            if (len == 0) return 0;
            if (remaining == 0) return -1;
            int n = conn.in.read(b, off, (int) Math.min((long) len, remaining));
            if (n < 0) {
                conn.close();
                done = true;
                throw new java.net.ProtocolException("unexpected end of stream");
            }
            remaining -= n;
            if (remaining == 0) finish(keepAlive);
            return n;
        }

        @Override
        public int available() {
            return (int) Math.min(remaining, 0x7fffffffL);
        }
    }

    private static final class Chunked extends Body {
        private long remainingInChunk = -1;
        private boolean eof;
        private final boolean keepAlive;
        private final HeaderList trailers;

        Chunked(Conn conn, boolean keepAlive, HeaderList trailers) {
            super(conn);
            this.keepAlive = keepAlive;
            this.trailers = trailers;
        }

        private void nextChunk() throws IOException {
            if (remainingInChunk != -1) {
                String crlf = conn.in.readLine();
                if (crlf == null) throw new ProtocolException("unexpected end of chunked body");
            }
            String line = conn.in.readLine();
            if (line == null) throw new ProtocolException("unexpected end of chunked body");
            int semi = line.indexOf(';');
            if (semi >= 0) line = line.substring(0, semi);
            try {
                remainingInChunk = Long.parseLong(line.trim(), 16);
            } catch (NumberFormatException e) {
                throw new ProtocolException("Expected a hex chunk size but was " + line);
            }
            if (remainingInChunk == 0) {
                readHeaders(conn.in, trailers);
                eof = true;
                finish(keepAlive);
            }
        }

        @Override
        public int read(byte[] b, int off, int len) throws IOException {
            if (closed) throw new IOException("stream closed");
            if (len == 0) return 0;
            if (eof) return -1;
            if (remainingInChunk == 0 || remainingInChunk == -1) {
                nextChunk();
                if (eof) return -1;
            }
            int n = conn.in.read(b, off, (int) Math.min((long) len, remainingInChunk));
            if (n < 0) {
                conn.close();
                done = true;
                throw new ProtocolException("unexpected end of chunked body");
            }
            remainingInChunk -= n;
            return n;
        }
    }

    private static final class UntilClose extends Body {
        UntilClose(Conn conn) { super(conn); }

        @Override
        public int read(byte[] b, int off, int len) throws IOException {
            if (closed) throw new IOException("stream closed");
            if (len == 0) return 0;
            if (done) return -1;
            int n = conn.in.read(b, off, len);
            if (n < 0) {
                finish(false);
                return -1;
            }
            return n;
        }
    }

    /** Inflates on first read, so building the response never blocks on the body. */
    private static final class Lazy extends InputStream {
        private final InputStream raw;
        private final boolean gzip;
        private InputStream inflated;

        Lazy(InputStream raw, boolean gzip) {
            this.raw = raw;
            this.gzip = gzip;
        }

        private InputStream stream() throws IOException {
            if (inflated == null) {
                inflated = gzip ? new GZIPInputStream(raw) : new InflaterInputStream(raw);
            }
            return inflated;
        }

        @Override
        public int read() throws IOException {
            return stream().read();
        }

        @Override
        public int read(byte[] b, int off, int len) throws IOException {
            return stream().read(b, off, len);
        }

        @Override
        public void close() throws IOException {
            raw.close();
        }
    }

    /** Reads whatever remains of the body into memory (for error streams and redirects). */
    public static byte[] drain(InputStream in) throws IOException {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        byte[] buf = new byte[4096];
        int n;
        while ((n = in.read(buf, 0, buf.length)) >= 0) {
            out.write(buf, 0, n);
        }
        return out.toByteArray();
    }

    /** Closes the live connection without waiting for the body. */
    public void disconnect() {
        abandon();
    }
}
