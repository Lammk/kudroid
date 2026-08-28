package java.util;

/**
 * java.util.MissingResourceException — thrown when a resource lookup fails.
 *
 * Harmony's regex Lexer catches this around a predefined character-class lookup, so
 * the type has to exist for that catch clause to compile and link.
 */
public class MissingResourceException extends RuntimeException {
    private final String className;
    private final String key;

    public MissingResourceException(String s, String className, String key) {
        super(s);
        this.className = className;
        this.key = key;
    }

    public String getClassName() {
        return className;
    }

    public String getKey() {
        return key;
    }
}
