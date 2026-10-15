package java.lang.reflect;

public class InvocationTargetException extends Exception {

    private final Throwable target;

    public InvocationTargetException(Throwable target) {
        super(target == null ? null : target.toString());
        this.target = target;
    }

    public InvocationTargetException(Throwable target, String message) {
        super(message);
        this.target = target;
    }

    public Throwable getTargetException() {
        return target;
    }

    public Throwable getCause() {
        return target;
    }
}
