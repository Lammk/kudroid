package java.security;

public class NoSuchProviderException extends GeneralSecurityException {
    private static final long serialVersionUID = 8488704370124950857L;
    public NoSuchProviderException() { super(); }
    public NoSuchProviderException(String msg) { super(msg); }
}
