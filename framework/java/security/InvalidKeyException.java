package java.security;

public class InvalidKeyException extends GeneralSecurityException {
    private static final long serialVersionUID = 5698479920593359816L;
    public InvalidKeyException() { super(); }
    public InvalidKeyException(String msg) { super(msg); }
}
