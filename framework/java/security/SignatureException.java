package java.security;

public class SignatureException extends GeneralSecurityException {
    private static final long serialVersionUID = 5118833078046722976L;
    public SignatureException() { super(); }
    public SignatureException(String msg) { super(msg); }
    public SignatureException(String message, Throwable cause) { super(message, cause); }
    public SignatureException(Throwable cause) { super(cause); }
}
