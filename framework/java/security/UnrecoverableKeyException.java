package java.security;

public class UnrecoverableKeyException extends GeneralSecurityException {
    private static final long serialVersionUID = 7274063888063394080L;
    public UnrecoverableKeyException() { super(); }
    public UnrecoverableKeyException(String msg) { super(msg); }
}
