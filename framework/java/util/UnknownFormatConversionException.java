package java.util;

public class UnknownFormatConversionException extends IllegalFormatException {
    private static final long serialVersionUID = 1903082634982634L;
    private final String s;
    public UnknownFormatConversionException(String s) {
        if (s == null) throw new NullPointerException();
        this.s = s;
    }
    public String getConversion() { return s; }
    public String getMessage() { return String.format("Conversion = '%s'", s); }
}
