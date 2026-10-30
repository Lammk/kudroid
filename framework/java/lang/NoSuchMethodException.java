package java.lang;

public class NoSuchMethodException extends ReflectiveOperationException {

    public NoSuchMethodException() {
    }

    public NoSuchMethodException(String message) {
        super(message);
    }
}
