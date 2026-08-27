package java.lang;

public class ArrayIndexOutOfBoundsException extends IndexOutOfBoundsException {

    public ArrayIndexOutOfBoundsException() {
    }

    public ArrayIndexOutOfBoundsException(String message) {
        super(message);
    }

    public ArrayIndexOutOfBoundsException(int index) {
        super("index " + index);
    }
}
