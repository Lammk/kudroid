package javax.net.ssl;

public interface HostnameVerifier {
    boolean verify(String hostname, SSLSession session);
}
