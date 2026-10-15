package android.graphics;

/**
 * minimal android.graphics.typeface implementation.
 *
 * represents a font. for kudroid minimal framework, here is an emulation.
 */
public class Typeface {
    /** default sans-serif font. */
    public static final Typeface DEFAULT = new Typeface("sans-serif");
    /** default bold font. */
    public static final Typeface DEFAULT_BOLD = new Typeface("sans-serif-bold");
    /** monospace font. */
    public static final Typeface MONOSPACE = new Typeface("monospace");
    /** serif font. */
    public static final Typeface SERIF = new Typeface("serif");

    private final String mFamily;

    private Typeface(String family) {
        mFamily = family;
    }

    /**
     * create a font from a family and style.
     */
    public static Typeface create(String familyName, int style) {
        return new Typeface(familyName);
    }

    /**
     * create a font from an existing font and a style.
     */
    public static Typeface create(Typeface family, int style) {
        return family != null ? family : DEFAULT;
    }

    /**
     * returns the font family name.
     */
    public String getFamily() {
        return mFamily;
    }

    /**
     * returns the font style.
     */
    public int getStyle() {
        return 0;
    }
}
