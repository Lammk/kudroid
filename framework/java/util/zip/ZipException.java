package java.util.zip;

import java.io.IOException;

public class ZipException extends IOException {
    private static final long serialVersionUID = 8000196834066188981L;
    public ZipException() { super(); }
    public ZipException(String s) { super(s); }
}
