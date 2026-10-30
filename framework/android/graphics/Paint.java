package android.graphics;

/**
 * minimal android.graphics.paint implementation.
 *
 * stores style and color information for drawing. for minimal framework
 * of kudroid, it stores basic drawing properties.
 */
public class Paint {
    /** drawing style: fill. */
    public static final int STYLE_FILL = 0;
    /** drawing type: stroke. */
    public static final int STYLE_STROKE = 1;
    /** drawing style: fill and stroke. */
    public static final int STYLE_FILL_AND_STROKE = 2;

    /** alignment: left. */
    public static final int ALIGN_LEFT = 0;
    /** alignment: center. */
    public static final int ALIGN_CENTER = 1;
    /** alignment: right. */
    public static final int ALIGN_RIGHT = 2;

    private int mColor = 0xFF000000;
    private int mStyle = STYLE_FILL;
    private float mStrokeWidth = 0.0f;
    private float mTextSize = 12.0f;
    private int mTextAlign = ALIGN_LEFT;
    private boolean mAntiAlias = false;
    private Typeface mTypeface = null;
    private ColorFilter mColorFilter = null;
    private float mShadowRadius, mShadowDx, mShadowDy;
    private int mShadowColor;

    public Paint() {
    }

    public Paint(int flags) {
    }

    public Paint(Paint paint) {
        if (paint != null) set(paint);
    }

    /** Copy every drawing attribute of |src| into this paint. */
    public void set(Paint src) {
        if (src == this || src == null) return;
        mColor = src.mColor;
        mStyle = src.mStyle;
        mEnumStyle = src.mEnumStyle;
        mEnumAlign = src.mEnumAlign;
        mStrokeWidth = src.mStrokeWidth;
        mTextSize = src.mTextSize;
        mTextAlign = src.mTextAlign;
        mAntiAlias = src.mAntiAlias;
        mTypeface = src.mTypeface;
        mColorFilter = src.mColorFilter;
        mShadowRadius = src.mShadowRadius;
        mShadowDx = src.mShadowDx;
        mShadowDy = src.mShadowDy;
        mShadowColor = src.mShadowColor;
    }

    public void setColor(int color) {
        mColor = color;
    }

    public int getColor() {
        return mColor;
    }

    public void setARGB(int a, int r, int g, int b) {
        setColor(((a & 255) << 24) | ((r & 255) << 16) | ((g & 255) << 8) | (b & 255));
    }

    public void setShadowLayer(float radius, float dx, float dy, int color) {
        mShadowRadius = Math.max(0, radius);
        mShadowDx = dx;
        mShadowDy = dy;
        mShadowColor = color;
    }

    public void clearShadowLayer() { mShadowRadius = 0; }
    public float getShadowRadius() { return mShadowRadius; }
    public float getShadowDx() { return mShadowDx; }
    public float getShadowDy() { return mShadowDy; }
    public int getShadowColor() { return mShadowColor; }

    public void setStyle(int style) {
        mStyle = style;
        mEnumStyle = style == STYLE_STROKE ? Style.STROKE
                : style == STYLE_FILL_AND_STROKE ? Style.FILL_AND_STROKE : Style.FILL;
    }

    public Style getStyle() {
        return mEnumStyle;
    }

    public int getStyleInt() {
        return mStyle;
    }

    public void setStrokeWidth(float width) {
        mStrokeWidth = width;
    }

    public float getStrokeWidth() {
        return mStrokeWidth;
    }

    public void setTextSize(float textSize) {
        mTextSize = textSize;
    }

    public float getTextSize() {
        return mTextSize;
    }

    public void setTextAlign(int align) {
        mTextAlign = align;
        mEnumAlign = align == 1 ? Align.CENTER : align == 2 ? Align.RIGHT : Align.LEFT;
    }

    public Align getTextAlign() {
        return mEnumAlign;
    }

    public int getTextAlignInt() {
        return mTextAlign;
    }

    public void setAntiAlias(boolean aa) {
        mAntiAlias = aa;
    }

    public boolean isAntiAlias() {
        return mAntiAlias;
    }

    public Typeface setTypeface(Typeface typeface) {
        Typeface previous = mTypeface;
        mTypeface = typeface;
        return previous;
    }

    public Typeface getTypeface() {
        return mTypeface;
    }

    public ColorFilter setColorFilter(ColorFilter colorFilter) {
        mColorFilter = colorFilter;
        return colorFilter;
    }

    public int getAlpha() {
        return mColor >>> 24;
    }

    public void setAlpha(int a) {
        mColor = ((a & 255) << 24) | (mColor & 0x00FFFFFF);
    }

    public ColorFilter getColorFilter() {
        return mColorFilter;
    }

    /**
     * returns the width of the given text.
     */
    public float measureText(String text) {
        return text != null ? text.length() * mTextSize * 0.5f : 0.0f;
    }

    public float measureText(String text, int start, int end) {
        if (text == null) throw new IllegalArgumentException("text is null");
        if (start < 0 || end < start || end > text.length()) throw new IndexOutOfBoundsException();
        return measureText(text.substring(start, end));
    }

    /**
     * returns font metrics.
     */
    public float getFontMetrics() {
        return mTextSize;
    }

    /**
     * Fill in vertical text extents.
     *
     * TextView and every custom text layout uses these to place baselines, so the
     * numbers have to be self-consistent even though KuDroid has no real font
     * metrics: ascent above the baseline is negative, descent below is positive, and
     * top/bottom add the leading. Zeroes here collapse every line onto one baseline.
     */
    public int getFontMetricsInt(FontMetricsInt fmi) {
        if (fmi == null) return (int) mTextSize;
        // Proportions of a typical sans-serif face; scaled with the text size so
        // layout still tracks setTextSize().
        fmi.ascent = -(int) (mTextSize * 0.80f);
        fmi.descent = (int) (mTextSize * 0.20f);
        fmi.top = -(int) (mTextSize * 0.93f);
        fmi.bottom = (int) (mTextSize * 0.25f);
        fmi.leading = 0;
        return fmi.descent - fmi.ascent;
    }

    public FontMetricsInt getFontMetricsInt() {
        FontMetricsInt fmi = new FontMetricsInt();
        getFontMetricsInt(fmi);
        return fmi;
    }

    /** Stroke end style. Enum-like instances so identity comparisons work. */
    public static final class Cap {
        public static final Cap BUTT = new Cap("BUTT", 0);
        public static final Cap ROUND = new Cap("ROUND", 1);
        public static final Cap SQUARE = new Cap("SQUARE", 2);

        private final String mName;
        private final int mValue;

        private Cap(String name, int value) {
            mName = name;
            mValue = value;
        }

        public int nativeInt() { return mValue; }
        public String name() { return mName; }
        public int ordinal() { return mValue; }
        public static Cap[] values() { return new Cap[] { BUTT, ROUND, SQUARE }; }

        @Override
        public String toString() { return mName; }
    }

    /**
     * Integer font metrics.
     *
     * Was an empty stub, so `fmi.ascent` threw NoSuchFieldError. The fields are
     * public and written directly by Paint and read directly by layout code, which
     * is why they must exist under their exact AOSP names.
     */
    public static class FontMetricsInt {
        /** Distance above the baseline for the tallest glyph; negative. */
        public int top;
        /** Recommended distance above the baseline; negative. */
        public int ascent;
        /** Recommended distance below the baseline; positive. */
        public int descent;
        /** Distance below the baseline for the lowest glyph; positive. */
        public int bottom;
        /** Extra space between lines. */
        public int leading;

        public FontMetricsInt() {}

        @Override
        public String toString() {
            return "FontMetricsInt: top=" + top + " ascent=" + ascent +
                   " descent=" + descent + " bottom=" + bottom + " leading=" + leading;
        }
    }

    /** Float font metrics; same fields, same reason. */
    public static class FontMetrics {
        public float top;
        public float ascent;
        public float descent;
        public float bottom;
        public float leading;

        public FontMetrics() {}
    }

    /** Stroke corner style. */
    public static final class Join {
        public static final Join MITER = new Join("MITER", 0);
        public static final Join ROUND = new Join("ROUND", 1);
        public static final Join BEVEL = new Join("BEVEL", 2);

        private final String mName;
        private final int mValue;

        private Join(String name, int value) {
            mName = name;
            mValue = value;
        }

        public int nativeInt() { return mValue; }
        public String name() { return mName; }
        public int ordinal() { return mValue; }
        public static Join[] values() { return new Join[] { MITER, ROUND, BEVEL }; }

        @Override
        public String toString() { return mName; }
    }


    public enum Style {
        FILL(0),
        STROKE(1),
        FILL_AND_STROKE(2);

        Style(int ni) { nativeInt = ni; }
        final int nativeInt;
    }

    public enum Align {
        LEFT(0),
        CENTER(1),
        RIGHT(2);

        Align(int ni) { nativeInt = ni; }
        final int nativeInt;
    }

    private Style mEnumStyle = Style.FILL;
    private Align mEnumAlign = Align.LEFT;
    private Cap mCap = Cap.BUTT;
    private Join mJoin = Join.MITER;
    private Shader mShader = null;
    private Xfermode mXfermode = null;

    public void setStyle(Style style) {
        mEnumStyle = style != null ? style : Style.FILL;
        mStyle = mEnumStyle.nativeInt;
    }

    public Style getStyleEnum() {
        return mEnumStyle;
    }

    public void setTextAlign(Align align) {
        mEnumAlign = align != null ? align : Align.LEFT;
        mTextAlign = mEnumAlign.nativeInt;
    }

    public Align getTextAlignEnum() {
        return mEnumAlign;
    }

    public void setStrokeCap(Cap cap) {
        mCap = cap != null ? cap : Cap.BUTT;
    }

    public Cap getStrokeCap() {
        return mCap;
    }

    public void setStrokeJoin(Join join) {
        mJoin = join != null ? join : Join.MITER;
    }

    public Join getStrokeJoin() {
        return mJoin;
    }

    public void setShader(Shader shader) {
        mShader = shader;
    }

    public Shader getShader() {
        return mShader;
    }

    public Xfermode setXfermode(Xfermode xfermode) {
        Xfermode previous = mXfermode;
        mXfermode = xfermode;
        return previous;
    }

    public Xfermode getXfermode() {
        return mXfermode;
    }

    public void getTextBounds(String text, int start, int end, Rect bounds) {
        if (bounds != null) {
            float width = measureText(text != null ? text.substring(start, end) : "");
            bounds.set(0, -(int) mTextSize, (int) width, 0);
        }
    }
}
