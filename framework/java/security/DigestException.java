package java.security;

public class DigestException extends GeneralSecurityException {
    private static final long serialVersionUID = 5821450303093652569L;
    public DigestException() { super(); }
    public DigestException(String msg) { super(msg); }
    public DigestException(String message, Throwable cause) { super(message, cause); }
    public DigestException(Throwable cause) { super(cause); }
}
