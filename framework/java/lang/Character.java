package java.lang;

/**
 * java.lang.Character.
 *
 * The classification methods are ASCII-exact and give a sensible approximation above
 * U+007F. There is no Unicode character database on the device: shipping one would add
 * megabytes to framework.dex for behaviour almost no app depends on. Where a precise
 * answer is impossible, the choice errs towards "letter" for assigned non-ASCII code
 * points, which is what text-processing code assumes.
 *
 * getType() and the general-category constants exist because java.util.regex needs them
 * to compile \p{L}, \p{Nd} and friends; the mapping is exact for ASCII and coarse above
 * it, so those classes work for Latin text and degrade gracefully elsewhere.
 */
public final class Character implements Comparable<Character> {

    public static final char MIN_VALUE = '\u0000';
    public static final char MAX_VALUE = '\uffff';
    public static final int MIN_RADIX = 2;
    public static final int MAX_RADIX = 36;
    // char.class compiles to a read of this field, so it must hold the real
    // primitive Class rather than null. See Class.getPrimitiveClass.
    @SuppressWarnings("unchecked")
    public static final Class<Character> TYPE =
            (Class<Character>) Class.getPrimitiveClass("char");

    // Surrogate and code-point range constants.
    public static final char MIN_HIGH_SURROGATE = '\uD800';
    public static final char MAX_HIGH_SURROGATE = '\uDBFF';
    public static final char MIN_LOW_SURROGATE = '\uDC00';
    public static final char MAX_LOW_SURROGATE = '\uDFFF';
    public static final char MIN_SURROGATE = MIN_HIGH_SURROGATE;
    public static final char MAX_SURROGATE = MAX_LOW_SURROGATE;
    public static final int MIN_SUPPLEMENTARY_CODE_POINT = 0x010000;
    public static final int MIN_CODE_POINT = 0x000000;
    public static final int MAX_CODE_POINT = 0X10FFFF;

    // Unicode general categories, values as defined by the Unicode standard.
    public static final byte UNASSIGNED = 0;
    public static final byte UPPERCASE_LETTER = 1;
    public static final byte LOWERCASE_LETTER = 2;
    public static final byte TITLECASE_LETTER = 3;
    public static final byte MODIFIER_LETTER = 4;
    public static final byte OTHER_LETTER = 5;
    public static final byte NON_SPACING_MARK = 6;
    public static final byte ENCLOSING_MARK = 7;
    public static final byte COMBINING_SPACING_MARK = 8;
    public static final byte DECIMAL_DIGIT_NUMBER = 9;
    public static final byte LETTER_NUMBER = 10;
    public static final byte OTHER_NUMBER = 11;
    public static final byte SPACE_SEPARATOR = 12;
    public static final byte LINE_SEPARATOR = 13;
    public static final byte PARAGRAPH_SEPARATOR = 14;
    public static final byte CONTROL = 15;
    public static final byte FORMAT = 16;
    public static final byte PRIVATE_USE = 18;
    public static final byte SURROGATE = 19;
    public static final byte DASH_PUNCTUATION = 20;
    public static final byte START_PUNCTUATION = 21;
    public static final byte END_PUNCTUATION = 22;
    public static final byte CONNECTOR_PUNCTUATION = 23;
    public static final byte OTHER_PUNCTUATION = 24;
    public static final byte MATH_SYMBOL = 25;
    public static final byte CURRENCY_SYMBOL = 26;
    public static final byte MODIFIER_SYMBOL = 27;
    public static final byte OTHER_SYMBOL = 28;
    public static final byte INITIAL_QUOTE_PUNCTUATION = 29;
    public static final byte FINAL_QUOTE_PUNCTUATION = 30;

    public static final byte DIRECTIONALITY_UNDEFINED = -1;
    public static final byte DIRECTIONALITY_LEFT_TO_RIGHT = 0;

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

    // ── classification ──
    // Each has a char and an int (code point) overload. Harmony's regex passes code
    // points, and letting those bind to the char version would silently truncate
    // anything above U+FFFF.

    public static boolean isDigit(char c) {
        return c >= '0' && c <= '9';
    }

    public static boolean isDigit(int c) {
        return c >= '0' && c <= '9';
    }

    public static boolean isUpperCase(char c) {
        return c >= 'A' && c <= 'Z';
    }

    public static boolean isUpperCase(int c) {
        return c >= 'A' && c <= 'Z';
    }

    public static boolean isLowerCase(char c) {
        return c >= 'a' && c <= 'z';
    }

    public static boolean isLowerCase(int c) {
        return c >= 'a' && c <= 'z';
    }

    public static boolean isTitleCase(char c) {
        return false;
    }

    public static boolean isTitleCase(int c) {
        return false;
    }

    /** Non-ASCII assigned code points are treated as letters; see the class comment. */
    public static boolean isLetter(char c) {
        return isLetter((int) c);
    }

    public static boolean isLetter(int c) {
        if (c >= 'A' && c <= 'Z') return true;
        if (c >= 'a' && c <= 'z') return true;
        if (c < 128) return false;
        // Above ASCII: exclude the ranges that are definitely not letters.
        if (c >= 0x2000 && c <= 0x206F) return false;   // general punctuation
        if (c >= 0x20A0 && c <= 0x20CF) return false;   // currency symbols
        if (c >= 0xD800 && c <= 0xDFFF) return false;   // surrogates
        if (c >= 0xE000 && c <= 0xF8FF) return false;   // private use
        if (c >= 0xFFF0) return false;                  // specials
        return true;
    }

    public static boolean isLetterOrDigit(char c) {
        return isLetter(c) || isDigit(c);
    }

    public static boolean isLetterOrDigit(int c) {
        return isLetter(c) || isDigit(c);
    }

    public static boolean isAlphabetic(int c) {
        return isLetter(c);
    }

    public static boolean isWhitespace(char c) {
        return isWhitespace((int) c);
    }

    public static boolean isWhitespace(int c) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == 0x0b) return true;
        if (c == 0x1c || c == 0x1d || c == 0x1e || c == 0x1f) return true;
        if (c == 0x1680 || (c >= 0x2000 && c <= 0x200a && c != 0x2007)) return true;
        return c == 0x2028 || c == 0x2029 || c == 0x205f || c == 0x3000;
    }

    public static boolean isSpaceChar(char c) {
        return isSpaceChar((int) c);
    }

    public static boolean isSpaceChar(int c) {
        if (c == ' ' || c == 0x00a0 || c == 0x1680) return true;
        if (c >= 0x2000 && c <= 0x200a) return true;
        return c == 0x2028 || c == 0x2029 || c == 0x202f || c == 0x205f || c == 0x3000;
    }

    public static boolean isISOControl(char c) {
        return isISOControl((int) c);
    }

    public static boolean isISOControl(int c) {
        return (c >= 0x00 && c <= 0x1f) || (c >= 0x7f && c <= 0x9f);
    }

    public static boolean isDefined(char c) {
        return isDefined((int) c);
    }

    /** True for every code point in range; there is no table of unassigned points. */
    public static boolean isDefined(int c) {
        return c >= MIN_CODE_POINT && c <= MAX_CODE_POINT;
    }

    public static boolean isMirrored(char c) {
        return isMirrored((int) c);
    }

    public static boolean isMirrored(int c) {
        return c == '(' || c == ')' || c == '[' || c == ']' || c == '{' || c == '}'
                || c == '<' || c == '>';
    }

    public static boolean isJavaIdentifierStart(char c) {
        return isJavaIdentifierStart((int) c);
    }

    public static boolean isJavaIdentifierStart(int c) {
        return isLetter(c) || c == '$' || c == '_';
    }

    public static boolean isJavaIdentifierPart(char c) {
        return isJavaIdentifierPart((int) c);
    }

    public static boolean isJavaIdentifierPart(int c) {
        return isLetterOrDigit(c) || c == '$' || c == '_' || isIdentifierIgnorable(c);
    }

    public static boolean isUnicodeIdentifierStart(char c) {
        return isUnicodeIdentifierStart((int) c);
    }

    public static boolean isUnicodeIdentifierStart(int c) {
        return isLetter(c);
    }

    public static boolean isUnicodeIdentifierPart(char c) {
        return isUnicodeIdentifierPart((int) c);
    }

    public static boolean isUnicodeIdentifierPart(int c) {
        return isLetterOrDigit(c) || c == '_' || isIdentifierIgnorable(c);
    }

    public static boolean isIdentifierIgnorable(char c) {
        return isIdentifierIgnorable((int) c);
    }

    public static boolean isIdentifierIgnorable(int c) {
        if (c >= 0x00 && c <= 0x08) return true;
        if (c >= 0x0e && c <= 0x1b) return true;
        if (c >= 0x7f && c <= 0x9f) return true;
        return c >= 0x200b && c <= 0x200f;
    }

    /**
     * Unicode general category.
     *
     * Exact for ASCII, coarse above it: letters report OTHER_LETTER rather than their
     * true script-specific category. That is enough for regex character classes to
     * behave correctly on \p{L}, \p{N}, \p{P} and \p{Z} for Latin input.
     */
    public static int getType(char c) {
        return getType((int) c);
    }

    public static int getType(int c) {
        if (c < 0 || c > MAX_CODE_POINT) return UNASSIGNED;
        if (c <= 0x1f || (c >= 0x7f && c <= 0x9f)) return CONTROL;
        if (c == ' ') return SPACE_SEPARATOR;
        if (c >= '0' && c <= '9') return DECIMAL_DIGIT_NUMBER;
        if (c >= 'A' && c <= 'Z') return UPPERCASE_LETTER;
        if (c >= 'a' && c <= 'z') return LOWERCASE_LETTER;
        if (c == '_') return CONNECTOR_PUNCTUATION;
        if (c == '-') return DASH_PUNCTUATION;
        if (c == '(' || c == '[' || c == '{') return START_PUNCTUATION;
        if (c == ')' || c == ']' || c == '}') return END_PUNCTUATION;
        if (c == '+' || c == '<' || c == '=' || c == '>' || c == '|' || c == '~'
                || c == '^') {
            return MATH_SYMBOL;
        }
        if (c == '$') return CURRENCY_SYMBOL;
        if (c == '`') return MODIFIER_SYMBOL;
        if (c < 128) return OTHER_PUNCTUATION;

        if (c >= 0xD800 && c <= 0xDFFF) return SURROGATE;
        if (c >= 0xE000 && c <= 0xF8FF) return PRIVATE_USE;
        if (c == 0x00a0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200a)
                || c == 0x202f || c == 0x205f || c == 0x3000) {
            return SPACE_SEPARATOR;
        }
        if (c == 0x2028) return LINE_SEPARATOR;
        if (c == 0x2029) return PARAGRAPH_SEPARATOR;
        if (c >= 0x200b && c <= 0x200f) return FORMAT;
        if (c >= 0x0300 && c <= 0x036f) return NON_SPACING_MARK;
        if (c >= 0x2010 && c <= 0x2015) return DASH_PUNCTUATION;
        if (c == 0x2018 || c == 0x201b || c == 0x201c || c == 0x201f) {
            return INITIAL_QUOTE_PUNCTUATION;
        }
        if (c == 0x2019 || c == 0x201a || c == 0x201d || c == 0x201e) {
            return FINAL_QUOTE_PUNCTUATION;
        }
        if (c >= 0x2016 && c <= 0x2027) return OTHER_PUNCTUATION;
        if (c >= 0x20a0 && c <= 0x20cf) return CURRENCY_SYMBOL;
        if (c >= 0x2100 && c <= 0x214f) return OTHER_SYMBOL;
        if (c >= 0x2150 && c <= 0x218f) return OTHER_NUMBER;
        if (c >= 0x2190 && c <= 0x2bff) return MATH_SYMBOL;
        if (c >= 0xff00 && c <= 0xffef) return OTHER_PUNCTUATION;
        return OTHER_LETTER;
    }

    public static byte getDirectionality(char c) {
        return DIRECTIONALITY_LEFT_TO_RIGHT;
    }

    // ── case conversion ──

    public static char toUpperCase(char c) {
        return (char) toUpperCase((int) c);
    }

    public static int toUpperCase(int c) {
        if (c >= 'a' && c <= 'z') return c - 32;
        // Latin-1 supplement: à-þ maps to À-Þ, but ÷ (0xf7) is not a letter.
        if (c >= 0x00e0 && c <= 0x00fe && c != 0x00f7) return c - 32;
        return c;
    }

    public static char toLowerCase(char c) {
        return (char) toLowerCase((int) c);
    }

    public static int toLowerCase(int c) {
        if (c >= 'A' && c <= 'Z') return c + 32;
        if (c >= 0x00c0 && c <= 0x00de && c != 0x00d7) return c + 32;
        return c;
    }

    public static char toTitleCase(char c) {
        return toUpperCase(c);
    }

    public static int toTitleCase(int c) {
        return toUpperCase(c);
    }

    // ── digits ──

    public static int digit(char c, int radix) {
        return digit((int) c, radix);
    }

    public static int digit(int c, int radix) {
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

    // ── surrogates and code points ──

    public static boolean isHighSurrogate(char c) {
        return c >= MIN_HIGH_SURROGATE && c <= MAX_HIGH_SURROGATE;
    }

    public static boolean isLowSurrogate(char c) {
        return c >= MIN_LOW_SURROGATE && c <= MAX_LOW_SURROGATE;
    }

    public static boolean isSurrogate(char c) {
        return isHighSurrogate(c) || isLowSurrogate(c);
    }

    public static boolean isSurrogatePair(char high, char low) {
        return isHighSurrogate(high) && isLowSurrogate(low);
    }

    public static boolean isSupplementaryCodePoint(int codePoint) {
        return codePoint >= MIN_SUPPLEMENTARY_CODE_POINT && codePoint <= MAX_CODE_POINT;
    }

    public static boolean isValidCodePoint(int codePoint) {
        return codePoint >= MIN_CODE_POINT && codePoint <= MAX_CODE_POINT;
    }

    public static int toCodePoint(char high, char low) {
        return MIN_SUPPLEMENTARY_CODE_POINT
                + ((high - MIN_HIGH_SURROGATE) << 10)
                + (low - MIN_LOW_SURROGATE);
    }

    public static int charCount(int codePoint) {
        return codePoint >= MIN_SUPPLEMENTARY_CODE_POINT ? 2 : 1;
    }

    public static char highSurrogate(int codePoint) {
        return (char) (MIN_HIGH_SURROGATE
                + ((codePoint - MIN_SUPPLEMENTARY_CODE_POINT) >> 10));
    }

    public static char lowSurrogate(int codePoint) {
        return (char) (MIN_LOW_SURROGATE
                + ((codePoint - MIN_SUPPLEMENTARY_CODE_POINT) & 0x3ff));
    }

    /** UTF-16 units for a code point: one char, or a surrogate pair. */
    public static char[] toChars(int codePoint) {
        if (!isSupplementaryCodePoint(codePoint)) {
            return new char[] { (char) codePoint };
        }
        return new char[] { highSurrogate(codePoint), lowSurrogate(codePoint) };
    }

    public static int toChars(int codePoint, char[] dst, int dstIndex) {
        if (!isSupplementaryCodePoint(codePoint)) {
            dst[dstIndex] = (char) codePoint;
            return 1;
        }
        dst[dstIndex] = highSurrogate(codePoint);
        dst[dstIndex + 1] = lowSurrogate(codePoint);
        return 2;
    }

    public static int codePointAt(CharSequence seq, int index) {
        final char high = seq.charAt(index);
        if (isHighSurrogate(high) && index + 1 < seq.length()) {
            final char low = seq.charAt(index + 1);
            if (isLowSurrogate(low)) return toCodePoint(high, low);
        }
        return high;
    }

    public static int codePointAt(char[] a, int index) {
        return codePointAt(a, index, a.length);
    }

    public static int codePointAt(char[] a, int index, int limit) {
        final char high = a[index];
        if (isHighSurrogate(high) && index + 1 < limit) {
            final char low = a[index + 1];
            if (isLowSurrogate(low)) return toCodePoint(high, low);
        }
        return high;
    }

    public static int codePointBefore(CharSequence seq, int index) {
        final char low = seq.charAt(index - 1);
        if (isLowSurrogate(low) && index - 2 >= 0) {
            final char high = seq.charAt(index - 2);
            if (isHighSurrogate(high)) return toCodePoint(high, low);
        }
        return low;
    }

    public static int codePointCount(CharSequence seq, int begin, int end) {
        int count = 0;
        for (int i = begin; i < end; ) {
            final char c = seq.charAt(i);
            if (isHighSurrogate(c) && i + 1 < end && isLowSurrogate(seq.charAt(i + 1))) {
                i += 2;
            } else {
                i++;
            }
            count++;
        }
        return count;
    }

    public static int offsetByCodePoints(CharSequence seq, int index, int codePointOffset) {
        int i = index;
        for (int n = 0; n < codePointOffset && i < seq.length(); n++) {
            final char c = seq.charAt(i);
            if (isHighSurrogate(c) && i + 1 < seq.length() && isLowSurrogate(seq.charAt(i + 1))) {
                i += 2;
            } else {
                i++;
            }
        }
        return i;
    }

    public static int reverseBytes(char c) {
        return ((c & 0xff) << 8) | ((c >> 8) & 0xff);
    }
}
