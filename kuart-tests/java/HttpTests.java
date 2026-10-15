import java.io.FileNotFoundException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.CookieManager;
import java.net.HttpURLConnection;
import java.net.SocketTimeoutException;
import java.net.URI;
import java.net.URL;
import java.net.URLDecoder;
import java.net.URLEncoder;

public final class HttpTests {
    private static void check(boolean ok, String name) { TestSupport.check(ok, name); }

    public static String run(int port) {
        TestSupport.report.setLength(0);
        try { base64Unit(); } catch (Throwable t) { TestSupport.fail("base64Unit", t); }
        try { gzipUnit(); } catch (Throwable t) { TestSupport.fail("gzipUnit", t); }
        try { urls(); } catch (Throwable t) { TestSupport.fail("urls", t); }
        try { uris(); } catch (Throwable t) { TestSupport.fail("uris", t); }
        try { encoding(); } catch (Throwable t) { TestSupport.fail("encoding", t); }
        try { http(port); } catch (Throwable t) { TestSupport.fail("http", t); }
        return TestSupport.report.toString();
    }

    private static HttpURLConnection open(String url) throws Exception {
        return (HttpURLConnection) new URL(url).openConnection();
    }

    private static String get(String url) throws Exception {
        HttpURLConnection c = open(url);
        return TestSupport.read(c.getInputStream());
    }

    static void base64Unit() throws Exception {
        String[] enc = {"Zg==", "Zm8=", "Zm9v", "Zm9vYg==", "Zm9vYmE=", "Zm9vYmFy"};
        String[] dec = {"f", "fo", "foo", "foob", "fooba", "foobar"};
        for (int i = 0; i < enc.length; i++) {
            String got;
            try {
                got = new String(java.util.Base64.getMimeDecoder().decode(enc[i]));
            } catch (Throwable t) {
                got = "threw " + t;
            }
            check(got.equals(dec[i]), "base64 decode " + enc[i] + " -> " + got);
        }
        StringBuilder wrapped = new StringBuilder();
        for (int i = 0; i < 40; i++) wrapped.append("Zm9vYmFy\n");
        byte[] out = java.util.Base64.getMimeDecoder().decode(wrapped.toString().trim());
        check(out.length == 240, "base64 decode with newlines: " + out.length);
    }

    static void gzipUnit() throws Exception {
        java.io.ByteArrayOutputStream bo = new java.io.ByteArrayOutputStream();
        java.util.zip.GZIPOutputStream gz = new java.util.zip.GZIPOutputStream(bo);
        gz.write("round trip".getBytes());
        gz.close();
        String back = TestSupport.read(new java.util.zip.GZIPInputStream(new java.io.ByteArrayInputStream(bo.toByteArray())));
        check(back.equals("round trip"), "gzip stream round trip: " + back);
    }

    static void urls() throws Exception {
        URL u = new URL("http://user:pw@Example.com:8080/a/b/c.html?x=1&y=2#frag");
        check(u.getProtocol().equals("http") && u.getHost().equals("Example.com") && u.getPort() == 8080, "url authority");
        check(u.getPath().equals("/a/b/c.html") && u.getQuery().equals("x=1&y=2") && u.getRef().equals("frag"), "url path/query/ref");
        check(u.getFile().equals("/a/b/c.html?x=1&y=2") && "user:pw".equals(u.getUserInfo()), "url file/userinfo");
        check(new URL("https://example.com").getPort() == -1 && new URL("https://example.com").getDefaultPort() == 443, "default port");
        check(new URL(u, "d.html").toString().equals("http://user:pw@Example.com:8080/a/b/d.html"), "relative sibling");
        check(new URL(u, "../e").toString().equals("http://user:pw@Example.com:8080/a/e"), "relative parent");
        check(new URL(u, "/root").getPath().equals("/root"), "relative absolute path");
        check(new URL(u, "//other.org/z").getHost().equals("other.org"), "relative network path");
        check(new URL(u, "https://secure.org/").getProtocol().equals("https"), "relative absolute url");
        check(new URL(u, "?q=2").toString().equals("http://user:pw@Example.com:8080/a/b/c.html?q=2"), "relative query");
        check(new URL("http://[::1]:81/x").getHost().equals("[::1]") && new URL("http://[::1]:81/x").getPort() == 81, "ipv6 host");
        try {
            new URL("nonsense");
            check(false, "no protocol rejected");
        } catch (java.net.MalformedURLException e) {
            check(true, "no protocol rejected");
        }
        check(new URL("http://a.com/x").equals(new URL("HTTP://A.COM:80/x")), "url equality ignores case and default port");
        check(new URL("file:/tmp/x").getPath().equals("/tmp/x"), "file url");
    }

    static void uris() throws Exception {
        URI u = new URI("http://user@host.com:99/p/q%20r?k=v%26w#f");
        check("http".equals(u.getScheme()) && "host.com".equals(u.getHost()) && u.getPort() == 99, "uri authority");
        check("user".equals(u.getUserInfo()) && "/p/q r".equals(u.getPath()) && "/p/q%20r".equals(u.getRawPath()), "uri path decoding");
        check("k=v&w".equals(u.getQuery()) && "k=v%26w".equals(u.getRawQuery()) && "f".equals(u.getFragment()), "uri query");
        URI base = new URI("http://a.com/b/c/d?q");
        check(base.resolve("e").toString().equals("http://a.com/b/c/e"), "uri resolve sibling");
        check(base.resolve("../e").toString().equals("http://a.com/b/e"), "uri resolve parent");
        check(base.resolve("/x/y").toString().equals("http://a.com/x/y"), "uri resolve absolute");
        check(base.resolve("http://z.com/").toString().equals("http://z.com/"), "uri resolve absolute uri");
        check(new URI("http://a.com/a/./b/../c").normalize().getPath().equals("/a/c"), "uri normalize");
        check(new URI("mailto:a@b.c").isOpaque() && "a@b.c".equals(new URI("mailto:a@b.c").getSchemeSpecificPart()), "opaque uri");
        try {
            new URI("http://a.com/b c");
            check(false, "space rejected");
        } catch (java.net.URISyntaxException e) {
            check(true, "space rejected");
        }
        check(new URI("http", "h.com", "/a b", null).toString().equals("http://h.com/a%20b"), "uri quoting constructor");
        check(URI.create("HTTP://A.com/x").equals(URI.create("http://a.com/x")), "uri equality");
    }

    static void encoding() throws Exception {
        check(URLEncoder.encode("a b&c=d/é").equals("a+b%26c%3Dd%2F%C3%A9"), "urlencode");
        check(URLDecoder.decode("a+b%26c%3Dd%2F%C3%A9").equals("a b&c=d/é"), "urldecode");
        check(URLDecoder.decode("plain").equals("plain"), "urldecode plain");
    }

    static void http(int port) throws Exception {
        String base = "http://127.0.0.1:" + port;
        check(get(base + "/hello").equals("hello world"), "GET content-length body");
        check(get(base + "/chunked").equals("abcdefgh"), "chunked body");
        HttpURLConnection gz = open(base + "/gzip");
        check(gz.getResponseCode() == 200, "gzip status");
        java.io.ByteArrayOutputStream gzOut = new java.io.ByteArrayOutputStream();
        try {
            InputStream gin = gz.getInputStream();
            byte[] gbuf = new byte[64];
            int gn;
            while ((gn = gin.read(gbuf, 0, gbuf.length)) >= 0) gzOut.write(gbuf, 0, gn);
        } catch (Throwable t) {
            check(false, "gzip read threw " + t);
        }
        check(gzOut.size() == 15, "gzip body length " + gzOut.size());
        check(new String(gzOut.toByteArray()).equals("compressed text"), "gzip body inflated");
        check(gz.getHeaderField("Content-Encoding") == null, "gzip content-encoding hidden");
        check(get(base + "/redirect").equals("hello world"), "302 redirect followed");
        HttpURLConnection nr = open(base + "/redirect");
        nr.setInstanceFollowRedirects(false);
        check(nr.getResponseCode() == 302 && nr.getHeaderField("Location").equals("/hello"), "redirect not followed when disabled");

        HttpURLConnection post = open(base + "/echo");
        post.setDoOutput(true);
        post.setRequestProperty("Content-Type", "text/plain");
        OutputStream out = post.getOutputStream();
        out.write("payload".getBytes());
        out.close();
        check(post.getResponseCode() == 200 && TestSupport.read(post.getInputStream()).equals("payload"), "POST body echoed");
        check("POST".equals(post.getHeaderField("X-Method")) && "text/plain".equals(post.getHeaderField("X-Content-Type")) && "7".equals(post.getHeaderField("X-Length")), "POST method and headers");

        HttpURLConnection r303 = open(base + "/redirect-post");
        r303.setDoOutput(true);
        r303.getOutputStream().write("x".getBytes());
        check(TestSupport.read(r303.getInputStream()).equals("GET"), "303 turns POST into GET");

        String headers = get(base + "/headers").toLowerCase();
        check(headers.indexOf("host: 127.0.0.1:" + port) >= 0 && headers.indexOf("user-agent: ") >= 0 && headers.indexOf("accept-encoding: gzip") >= 0, "default request headers");
        HttpURLConnection h = open(base + "/headers");
        h.setRequestProperty("X-Custom", "42");
        check(TestSupport.read(h.getInputStream()).toLowerCase().indexOf("x-custom: 42") >= 0, "custom request header");

        HttpURLConnection nf = open(base + "/notfound");
        check(nf.getResponseCode() == 404, "404 code");
        try {
            nf.getInputStream();
            check(false, "404 throws FileNotFoundException");
        } catch (FileNotFoundException e) {
            check(true, "404 throws FileNotFoundException");
        }
        check(TestSupport.read(nf.getErrorStream()).equals("nope"), "404 error stream");
        HttpURLConnection boom = open(base + "/boom");
        try {
            boom.getInputStream();
            check(false, "500 throws IOException");
        } catch (FileNotFoundException e) {
            check(false, "500 throws IOException");
        } catch (java.io.IOException e) {
            check(e.getMessage().indexOf("500") >= 0, "500 throws IOException");
        }
        check(TestSupport.read(boom.getErrorStream()).equals("error body"), "500 error stream");

        check(get(base + "/query?q=a%20b&x=1").equals("/query?q=a%20b&x=1"), "request target keeps the query");
        HttpURLConnection head = open(base + "/hello");
        head.setRequestMethod("HEAD");
        check(head.getResponseCode() == 200 && head.getContentLength() == 11 && TestSupport.read(head.getInputStream()).length() == 0, "HEAD has headers but no body");
        check(get(base + "/close").equals("until-close"), "body read until close");

        HttpURLConnection slow = open(base + "/slow");
        slow.setReadTimeout(200);
        try {
            slow.getResponseCode();
            check(false, "read timeout");
        } catch (SocketTimeoutException e) {
            check(true, "read timeout");
        }

        // Keep-alive: sequential requests share one connection.
        get(base + "/hello");
        int before = Integer.parseInt(get(base + "/count").trim());
        get(base + "/hello");
        get(base + "/hello");
        int after = Integer.parseInt(get(base + "/count").trim());
        check(after == before, "keep-alive reuses the connection");

        HttpURLConnection info = open(base + "/hello");
        check(info.getResponseMessage().equals("OK") && info.getContentType().startsWith("text/plain") && info.getHeaderFieldKey(0) == null && info.getHeaderField(0).startsWith("HTTP/1.1 200"), "response message and headers");
        check(info.getHeaderFields().get(null) != null, "header map carries the status line");

        CookieManager cm = new CookieManager();
        java.net.CookieHandler.setDefault(cm);
        get(base + "/setcookie");
        check(get(base + "/needcookie").equals("sid=abc"), "cookies stored and sent back");
        java.net.CookieHandler.setDefault(null);
        check(!get(base + "/needcookie").equals("sid=abc"), "no cookie handler, no cookie");

        try {
            open("http://no-such-host.invalid/").getResponseCode();
            check(false, "unknown host");
        } catch (java.net.UnknownHostException e) {
            check(true, "unknown host");
        }
        HttpURLConnection refused = open("http://127.0.0.1:1/");
        try {
            refused.getResponseCode();
            check(false, "connection refused");
        } catch (java.net.ConnectException e) {
            check(true, "connection refused");
        }
    }
}
