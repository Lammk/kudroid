package java.lang;

public class UnsatisfiedLinkError extends LinkageError {

    public UnsatisfiedLinkError() {
    }

    public UnsatisfiedLinkError(String message) {
        super(message);
    }
}
