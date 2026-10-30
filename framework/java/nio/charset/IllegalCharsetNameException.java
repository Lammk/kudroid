package java.nio.charset;

public class IllegalCharsetNameException extends IllegalArgumentException {
    private final String charsetName;
    public IllegalCharsetNameException(String name) { super(name); charsetName = name; }
    public String getCharsetName() { return charsetName; }
}
