package javax.net.ssl;

import java.util.Enumeration;

public interface SSLSessionContext {
    SSLSession getSession(byte[] sessionId);
    Enumeration<byte[]> getIds();
    void setSessionTimeout(int seconds);
    int getSessionTimeout();
    void setSessionCacheSize(int size);
    int getSessionCacheSize();
}
