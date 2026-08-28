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

    public Paint() {
    }

    public Paint(int flags) {
    }

    public Paint(Paint paint) {
        if (paint != null) {
            mColor = paint.mColor;
            mStyle = paint.mStyle;
            mStrokeWidth = paint.mStrokeWidth;
            mTextSize = paint.mTextSize;
            mTextAlign = paint.mTextAlign;
            mAntiAlias = paint.mAntiAlias;
            mTypeface = paint.mTypeface;
            mColorFilter = paint.mColorFilter;
        }
    }

    public void setColor(int color) {
        mColor = color;
    }

    public int getColor() {
        return mColor;
    }

    public void setStyle(int style) {
        mStyle = style;
    }

    public int getStyle() {
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
    }

    public int getTextAlign() {
        return mTextAlign;
    }

    public void setAntiAlias(boolean aa) {
        mAntiAlias = aa;
    }

    public boolean isAntiAlias() {
        return mAntiAlias;
    }

    public void setTypeface(Typeface typeface) {
        mTypeface = typeface;
    }

    public Typeface getTypeface() {
        return mTypeface;
    }

    public void setColorFilter(ColorFilter colorFilter) {
        mColorFilter = colorFilter;
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

}
