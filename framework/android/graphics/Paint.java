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

    public static class Cap {
        public Cap() {}
    }

    public static class FontMetricsInt {
        public FontMetricsInt() {}
    }

    public static class Join {
        public Join() {}
    }

}
