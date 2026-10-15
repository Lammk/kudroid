package java.text;

/**
 * java.text.ParseException — thrown when text cannot be parsed at a given offset.
 */
public class ParseException extends Exception {
    private final int errorOffset;

    public ParseException(String detailMessage, int location) {
        super(detailMessage);
        errorOffset = location;
    }

    public int getErrorOffset() {
        return errorOffset;
    }
}
