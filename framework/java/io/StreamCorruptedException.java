package java.io;

public class StreamCorruptedException extends ObjectStreamException {
    public StreamCorruptedException(String reason) { super(reason); }
    public StreamCorruptedException() { super(); }
}
