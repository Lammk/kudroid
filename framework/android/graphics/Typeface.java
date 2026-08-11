package android.graphics;

/**
 * Minimal android.graphics.Typeface implementation.
 *
 * Represents a font. For KuDroid's minimal framework, this is a stub.
 */
public class Typeface {
    /** Default sans-serif typeface. */
    public static final Typeface DEFAULT = new Typeface("sans-serif");
    /** Default bold typeface. */
    public static final Typeface DEFAULT_BOLD = new Typeface("sans-serif-bold");
    /** Monospace typeface. */
    public static final Typeface MONOSPACE = new Typeface("monospace");
    /** Serif typeface. */
    public static final Typeface SERIF = new Typeface("serif");

    private final String mFamily;

    private Typeface(String family) {
        mFamily = family;
    }

    /**
     * Create a typeface from a family and style.
     */
    public static Typeface create(String familyName, int style) {
        return new Typeface(familyName);
    }

    /**
     * Create a typeface from an existing one and a style.
     */
    public static Typeface create(Typeface family, int style) {
        return family != null ? family : DEFAULT;
    }

    /**
     * Return the typeface family name.
     */
    public String getFamily() {
        return mFamily;
    }

    /**
     * Return the typeface style.
     */
    public int getStyle() {
        return 0;
    }
}
