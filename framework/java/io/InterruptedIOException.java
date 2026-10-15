package java.io;

public class InterruptedIOException extends IOException {
    private static final long serialVersionUID = 4010599766104040804L;
    public int bytesTransferred = 0;

    public InterruptedIOException() { super(); }
    public InterruptedIOException(String s) { super(s); }
}
