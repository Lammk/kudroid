package java.security;

public class KeyManagementException extends GeneralSecurityException {
    private static final long serialVersionUID = 948447861248889361L;
    public KeyManagementException() { super(); }
    public KeyManagementException(String msg) { super(msg); }
    public KeyManagementException(String message, Throwable cause) { super(message, cause); }
    public KeyManagementException(Throwable cause) { super(cause); }
}
