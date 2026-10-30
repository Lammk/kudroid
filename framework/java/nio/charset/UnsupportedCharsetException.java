package java.nio.charset;

public class UnsupportedCharsetException extends IllegalArgumentException {
    private final String charsetName;
    public UnsupportedCharsetException(String name) { super(name); charsetName = name; }
    public String getCharsetName() { return charsetName; }
}
