package android.graphics;

/**
 * minimal android.graphics.typeface implementation.
 *
 * represents a font. for kudroid minimal framework, here is an emulation.
 */
public class Typeface {
    public static final int NORMAL = 0;
    public static final int BOLD = 1;
    public static final int ITALIC = 2;
    public static final int BOLD_ITALIC = 3;

    public static final Typeface DEFAULT = new Typeface("sans-serif", NORMAL);
    public static final Typeface DEFAULT_BOLD = new Typeface("sans-serif-bold", BOLD);
    public static final Typeface MONOSPACE = new Typeface("monospace", NORMAL);
    public static final Typeface SERIF = new Typeface("serif", NORMAL);
    public static final Typeface SANS_SERIF = new Typeface("sans-serif", NORMAL);

    private final String mFamily;
    private final int mStyle;

    private Typeface(String family) {
        this(family, NORMAL);
    }

    private Typeface(String family, int style) {
        mFamily = family;
        mStyle = style;
    }

    public static Typeface create(String familyName, int style) {
        return new Typeface(familyName, style);
    }

    public static Typeface create(Typeface family, int style) {
        return new Typeface(family != null ? family.getFamily() : "sans-serif", style);
    }

    public static Typeface defaultFromStyle(int style) {
        return create((String) null, style);
    }

    public static Typeface createFromAsset(android.content.res.AssetManager mgr, String path) {
        return new Typeface(path != null ? path : "sans-serif", NORMAL);
    }

    public static Typeface createFromFile(java.io.File file) {
        return new Typeface(file != null ? file.getPath() : "sans-serif", NORMAL);
    }

    public static Typeface createFromFile(String path) {
        return new Typeface(path != null ? path : "sans-serif", NORMAL);
    }

    public String getFamily() {
        return mFamily;
    }

    public int getStyle() {
        return mStyle;
    }

    public boolean isBold() {
        return (mStyle & BOLD) != 0;
    }

    public boolean isItalic() {
        return (mStyle & ITALIC) != 0;
    }
}

