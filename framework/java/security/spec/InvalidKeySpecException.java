package java.security.spec;

public class InvalidKeySpecException extends java.security.GeneralSecurityException {
    public InvalidKeySpecException() {}
    public InvalidKeySpecException(String message) { super(message); }
    public InvalidKeySpecException(String message, Throwable cause) { super(message, cause); }
    public InvalidKeySpecException(Throwable cause) { super(cause); }
}
