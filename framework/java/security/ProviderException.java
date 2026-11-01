package java.security;

public class ProviderException extends RuntimeException {
    public ProviderException() {}
    public ProviderException(String message) { super(message); }
    public ProviderException(String message, Throwable cause) { super(message, cause); }
    public ProviderException(Throwable cause) { super(cause); }
}
