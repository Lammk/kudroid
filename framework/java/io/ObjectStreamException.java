package java.io;

public abstract class ObjectStreamException extends IOException {
    protected ObjectStreamException(String message) { super(message); }
    protected ObjectStreamException() { super(); }
}
