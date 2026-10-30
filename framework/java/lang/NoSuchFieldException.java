package java.lang;

public class NoSuchFieldException extends ReflectiveOperationException {

    public NoSuchFieldException() {
    }

    public NoSuchFieldException(String message) {
        super(message);
    }
}
