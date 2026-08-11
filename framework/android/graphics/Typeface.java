package android.graphics;

/**
 * triển khai android.graphics.typeface tối thiểu.
 *
 * đại diện cho một phông chữ. đối với khuôn khổ tối thiểu của kudroid, đây là một mô phỏng.
 */
public class Typeface {
    /** phông chữ sans-serif mặc định. */
    public static final Typeface DEFAULT = new Typeface("sans-serif");
    /** phông chữ đậm mặc định. */
    public static final Typeface DEFAULT_BOLD = new Typeface("sans-serif-bold");
    /** phông chữ monospace. */
    public static final Typeface MONOSPACE = new Typeface("monospace");
    /** phông chữ serif. */
    public static final Typeface SERIF = new Typeface("serif");

    private final String mFamily;

    private Typeface(String family) {
        mFamily = family;
    }

    /**
     * tạo một phông chữ từ một họ và kiểu.
     */
    public static Typeface create(String familyName, int style) {
        return new Typeface(familyName);
    }

    /**
     * tạo một phông chữ từ một phông chữ hiện có và một kiểu.
     */
    public static Typeface create(Typeface family, int style) {
        return family != null ? family : DEFAULT;
    }

    /**
     * trả về tên họ phông chữ.
     */
    public String getFamily() {
        return mFamily;
    }

    /**
     * trả về kiểu phông chữ.
     */
    public int getStyle() {
        return 0;
    }
}
