package java.lang;

public class ExceptionInInitializerError extends LinkageError {

    private Throwable exception;

    public ExceptionInInitializerError() {
    }

    public ExceptionInInitializerError(String message) {
        super(message);
    }

    public ExceptionInInitializerError(Throwable cause) {
        this.exception = cause;
        initCause(cause);
    }

    public Throwable getException() {
        return exception;
    }
}
