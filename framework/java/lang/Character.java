package java.lang;

public final class Character implements Comparable<Character> {

    public static final char MIN_VALUE = '\u0000';
    public static final char MAX_VALUE = '\uffff';
    public static final int MIN_RADIX = 2;
    public static final int MAX_RADIX = 36;
    public static final Class<Character> TYPE = null;

    private final char value;

    public Character(char value) {
        this.value = value;
    }

    public static Character valueOf(char c) {
        return new Character(c);
    }

    public char charValue() {
        return value;
    }

    public int hashCode() {
        return value;
    }

    public boolean equals(Object other) {
        return (other instanceof Character) && ((Character) other).value == value;
    }

    public int compareTo(Character other) {
        return value - other.value;
    }

    public String toString() {
        return String.valueOf(value);
    }

    public static String toString(char c) {
        return String.valueOf(c);
    }

    public static int compare(char a, char b) {
        return a - b;
    }

    public static boolean isDigit(char c) {
        return c >= '0' && c <= '9';
    }

    public static boolean isUpperCase(char c) {
        return c >= 'A' && c <= 'Z';
    }

    public static boolean isLowerCase(char c) {
        return c >= 'a' && c <= 'z';
    }

    public static boolean isLetter(char c) {
        return isUpperCase(c) || isLowerCase(c) || c > 127;
    }

    public static boolean isLetterOrDigit(char c) {
        return isLetter(c) || isDigit(c);
    }

    public static boolean isAlphabetic(int c) {
        return isLetter((char) c);
    }

    public static boolean isWhitespace(char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == 0x0b;
    }

    public static boolean isSpaceChar(char c) {
        return c == ' ' || c == 0x00a0 || c == 0x2007 || c == 0x202f;
    }

    public static char toUpperCase(char c) {
        return isLowerCase(c) ? (char) (c - 32) : c;
    }

    public static char toLowerCase(char c) {
        return isUpperCase(c) ? (char) (c + 32) : c;
    }

    public static int digit(char c, int radix) {
        if (radix < MIN_RADIX || radix > MAX_RADIX) {
            return -1;
        }
        int v = -1;
        if (c >= '0' && c <= '9') {
            v = c - '0';
        } else if (c >= 'a' && c <= 'z') {
            v = c - 'a' + 10;
        } else if (c >= 'A' && c <= 'Z') {
            v = c - 'A' + 10;
        }
        return v < radix ? v : -1;
    }

    public static char forDigit(int digit, int radix) {
        if (digit < 0 || digit >= radix || radix < MIN_RADIX || radix > MAX_RADIX) {
            return '\u0000';
        }
        return digit < 10 ? (char) ('0' + digit) : (char) ('a' + digit - 10);
    }

    public static int getNumericValue(char c) {
        return digit(c, 36);
    }

    public static boolean isHighSurrogate(char c) {
        return c >= '\ud800' && c <= '\udbff';
    }

    public static boolean isLowSurrogate(char c) {
        return c >= '\udc00' && c <= '\udfff';
    }

    public static boolean isSurrogate(char c) {
        return isHighSurrogate(c) || isLowSurrogate(c);
    }

    public static int toCodePoint(char high, char low) {
        return 0x10000 + ((high - 0xd800) << 10) + (low - 0xdc00);
    }

    public static int charCount(int codePoint) {
        return codePoint >= 0x10000 ? 2 : 1;
    }
}
