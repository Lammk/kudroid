package java.lang;

public class Throwable {

    private String message;
    private Throwable cause;
    private StackTraceElement[] stackTrace;

    public Throwable() {
        this.stackTrace = new StackTraceElement[0];
    }

    public Throwable(String message) {
        this.message = message;
        this.stackTrace = new StackTraceElement[0];
    }

    public Throwable(String message, Throwable cause) {
        this.message = message;
        this.cause = cause;
        this.stackTrace = new StackTraceElement[0];
    }

    public Throwable(Throwable cause) {
        this.cause = cause;
        this.message = cause == null ? null : cause.toString();
        this.stackTrace = new StackTraceElement[0];
    }

    public String getMessage() {
        return message;
    }

    public String getLocalizedMessage() {
        return message;
    }

    public Throwable getCause() {
        return cause;
    }

    public Throwable initCause(Throwable cause) {
        this.cause = cause;
        return this;
    }

    public StackTraceElement[] getStackTrace() {
        return stackTrace;
    }

    public void setStackTrace(StackTraceElement[] trace) {
        this.stackTrace = trace;
    }

    public Throwable fillInStackTrace() {
        return this;
    }

    public void addSuppressed(Throwable t) {
    }

    public Throwable[] getSuppressed() {
        return new Throwable[0];
    }

    public void printStackTrace() {
        System.err.println(toString());
        for (int i = 0; i < stackTrace.length; i++) {
            System.err.println("\tat " + stackTrace[i]);
        }
        if (cause != null) {
            System.err.println("Caused by: ");
            cause.printStackTrace();
        }
    }

    public String toString() {
        String name = getClass().getName();
        return message == null ? name : name + ": " + message;
    }
}
