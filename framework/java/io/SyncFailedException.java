package java.io;

public class SyncFailedException extends IOException {
    private static final long serialVersionUID = -2353342684490002999L;
    public SyncFailedException(String desc) { super(desc); }
}
