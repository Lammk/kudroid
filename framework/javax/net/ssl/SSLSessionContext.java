package javax.net.ssl;

import java.util.Enumeration;
import java.util.Collections;

public interface SSLSessionContext {
    SSLSession getSession(byte[] sessionId);
    Enumeration<byte[]> getIds();
    void setSessionTimeout(int seconds) throws IllegalArgumentException;
    int getSessionTimeout();
    void setSessionCacheSize(int size) throws IllegalArgumentException;
    int getSessionCacheSize();
}
