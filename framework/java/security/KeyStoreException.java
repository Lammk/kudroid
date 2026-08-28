package java.security;

public class KeyStoreException extends GeneralSecurityException {
    public KeyStoreException() {}
    public KeyStoreException(String msg) { super(msg); }
    public KeyStoreException(String message, Throwable cause) { super(message, cause); }
    public KeyStoreException(Throwable cause) { super(cause); }
}
