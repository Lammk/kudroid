package java.util;

public class MissingFormatArgumentException extends IllegalFormatException {
    private static final long serialVersionUID = 19190415L;
    private final String s;
    public MissingFormatArgumentException(String s) {
        if (s == null) throw new NullPointerException();
        this.s = s;
    }
    public String getFormatSpecifier() { return s; }
}
