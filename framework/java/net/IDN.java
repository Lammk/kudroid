package java.net;

public final class IDN {
    public static final int ALLOW_UNASSIGNED = 1;
    public static final int USE_STD3_ASCII_RULES = 2;
    private IDN() {}
    public static String toASCII(String input, int flag) { return input; }
    public static String toASCII(String input) { return input; }
    public static String toUnicode(String input, int flag) { return input; }
    public static String toUnicode(String input) { return input; }
}
