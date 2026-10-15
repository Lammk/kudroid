package android.webkit;

public class SslError {
    public static final int SSL_NOTYETVALID = 0;
    public static final int SSL_EXPIRED = 1;
    public static final int SSL_IDMISMATCH = 2;
    public static final int SSL_UNTRUSTED = 3;
    public static final int SSL_DATE_INVALID = 4;
    public static final int SSL_INVALID = 5;

    public SslError(int error, String url) {}
    public int getPrimaryError() { return SSL_UNTRUSTED; }
    public String getUrl() { return ""; }
}
