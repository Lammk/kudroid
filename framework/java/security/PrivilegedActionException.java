package java.security;

public class PrivilegedActionException extends Exception {
    private static final long serialVersionUID = 4724086851538908602L;
    private final Exception exception;

    public PrivilegedActionException(Exception exception) {
        super((Throwable) null);
        this.exception = exception;
    }
    public Exception getException() { return exception; }
    public Throwable getCause() { return exception; }
    public String toString() {
        String name = getClass().getName();
        return exception == null ? name : name + ": " + exception.toString();
    }
}
