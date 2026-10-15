package java.nio.charset;

public class CoderResult {
    public static final CoderResult UNDERFLOW = new CoderResult(0, 0);
    public static final CoderResult OVERFLOW = new CoderResult(1, 0);

    private final int type;
    private final int length;

    private CoderResult(int type, int length) {
        this.type = type;
        this.length = length;
    }

    public boolean isUnderflow() { return type == 0; }
    public boolean isOverflow() { return type == 1; }
    public boolean isError() { return type > 1; }
    public boolean isMalformed() { return type == 2; }
    public boolean isUnmappable() { return type == 3; }
    public int length() { return length; }

    public static CoderResult malformedForLength(int length) {
        return new CoderResult(2, length);
    }

    public static CoderResult unmappableForLength(int length) {
        return new CoderResult(3, length);
    }
}
